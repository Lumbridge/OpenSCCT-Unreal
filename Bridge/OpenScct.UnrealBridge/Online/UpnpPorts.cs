using System;
using System.Globalization;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Xml.Linq;

namespace OpenScct.UnrealBridge.Online;

/// <summary>
/// UPnP port mapping for a listen host (the Godot client's game/Scripts/Online/UpnpPorts.cs, rl:net.upnp), without
/// Godot's Upnp class: an SSDP search for the router (an InternetGatewayDevice), its description for the WAN
/// connection service, then SOAP calls to it. It maps the game port (UDP, the same port outside and inside, a 24 h
/// lease, an earlier mapping of this port removed first, as Reloaded does) and says how it went, naming the port to
/// forward by hand when it did not. Runs on a worker thread; the search waits up to two seconds.
/// </summary>
public sealed class UpnpPorts
{
    /// <summary>Lease of a mapping, in seconds (Reloaded.Core 0x100bd5f0: UPNP_AddPortMapping lease 86400).</summary>
    public const int LeaseSeconds = 86400;

    private const string SearchTarget = "urn:schemas-upnp-org:device:InternetGatewayDevice:1";

    private static readonly string[] ServiceTypes =
    [
        "urn:schemas-upnp-org:service:WANIPConnection:2",
        "urn:schemas-upnp-org:service:WANIPConnection:1",
        "urn:schemas-upnp-org:service:WANPPPConnection:1",
    ];

    private readonly object _gate = new();
    private Gateway? _mapped;
    private int _mappedPort;
    private bool _removed;

    /// <summary>The router's public address once the mapping worked, else null.</summary>
    public string? ExternalAddress { get; private set; }

    /// <summary>Maps <paramref name="port"/>; the task's result is a diagnostic for the host log.</summary>
    public Task<string> MapAsync(int port) => Task.Run(() => Map(port));

    private string Map(int port)
    {
        try
        {
            Gateway? gateway = Discover(TimeSpan.FromSeconds(2));
            if (gateway is null)
            {
                return Manual(port, "no UPnP router answered");
            }

            // A mapping left by an earlier session goes first; a router without one answers with a fault, which is fine.
            gateway.Call("DeletePortMapping", Delete(port), out _);
            string args =
                "<NewRemoteHost></NewRemoteHost>" +
                $"<NewExternalPort>{port}</NewExternalPort>" +
                "<NewProtocol>UDP</NewProtocol>" +
                $"<NewInternalPort>{port}</NewInternalPort>" +
                $"<NewInternalClient>{gateway.LocalAddress}</NewInternalClient>" +
                "<NewEnabled>1</NewEnabled>" +
                "<NewPortMappingDescription>OpenSCCT game</NewPortMappingDescription>" +
                $"<NewLeaseDuration>{LeaseSeconds}</NewLeaseDuration>";
            if (!gateway.Call("AddPortMapping", args, out string? fault))
            {
                return Manual(port, "the router refused the mapping (" + fault + ")");
            }

            lock (_gate)
            {
                if (_removed)
                {
                    // The host closed while the router was being asked: the mapping just made is taken back.
                    gateway.Call("DeletePortMapping", Delete(port), out _);
                    return "UPnP: the host closed before the port was forwarded.";
                }

                _mapped = gateway;
                _mappedPort = port;
            }

            ExternalAddress = gateway.Query("GetExternalIPAddress", "NewExternalIPAddress");
            return string.Create(CultureInfo.InvariantCulture,
                $"UPnP: UDP port {port} is forwarded on the router ({ExternalAddress ?? "public address not given"}).");
        }
        catch (Exception e) when (e is SocketException or HttpRequestException or TaskCanceledException or System.Xml.XmlException or InvalidOperationException or UriFormatException)
        {
            return Manual(port, e.Message);
        }
    }

    private static string Delete(int port) =>
        $"<NewRemoteHost></NewRemoteHost><NewExternalPort>{port}</NewExternalPort><NewProtocol>UDP</NewProtocol>";

    private static string Manual(int port, string why) =>
        string.Create(CultureInfo.InvariantCulture,
            $"UPnP: {why}. Forward UDP port {port} on your router by hand, or players will join through the master's relay.");

    /// <summary>Removes the mapping this host made.</summary>
    public void Remove()
    {
        Gateway? gateway;
        int port;
        lock (_gate)
        {
            _removed = true;
            gateway = _mapped;
            port = _mappedPort;
            _mapped = null;
            _mappedPort = 0;
        }

        if (gateway is not null && port > 0)
        {
            try
            {
                gateway.Call("DeletePortMapping", Delete(port), out _);
            }
            catch (Exception e) when (e is HttpRequestException or TaskCanceledException or SocketException)
            {
                // The lease runs out on its own.
            }
        }
    }

    // ---- discovery ------------------------------------------------------------------------------------------------

    /// <summary>Sends the SSDP search twice and takes the first router whose description has a WAN connection service.</summary>
    private static Gateway? Discover(TimeSpan wait)
    {
        using var socket = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
        socket.SetSocketOption(SocketOptionLevel.IP, SocketOptionName.MulticastTimeToLive, 2);
        socket.Bind(new IPEndPoint(IPAddress.Any, 0));
        var group = new IPEndPoint(IPAddress.Parse("239.255.255.250"), 1900);
        byte[] search = Encoding.ASCII.GetBytes(
            "M-SEARCH * HTTP/1.1\r\n" +
            "HOST: 239.255.255.250:1900\r\n" +
            "MAN: \"ssdp:discover\"\r\n" +
            "MX: 2\r\n" +
            $"ST: {SearchTarget}\r\n\r\n");
        socket.SendTo(search, group);
        socket.SendTo(search, group);

        var buffer = new byte[4096];
        DateTime end = DateTime.UtcNow + wait;
        while (DateTime.UtcNow < end)
        {
            int left = (int)Math.Max(1, (end - DateTime.UtcNow).TotalMilliseconds);
            if (!socket.Poll(left * 1000, SelectMode.SelectRead))
            {
                break;
            }

            EndPoint from = new IPEndPoint(IPAddress.Any, 0);
            int n = socket.ReceiveFrom(buffer, ref from);
            string answer = Encoding.ASCII.GetString(buffer, 0, n);
            string? location = Header(answer, "LOCATION");
            if (location is null || !Uri.TryCreate(location, UriKind.Absolute, out Uri? uri))
            {
                continue;
            }

            try
            {
                if (Describe(uri) is { } gateway)
                {
                    return gateway;
                }
            }
            catch (Exception e) when (e is HttpRequestException or TaskCanceledException or System.Xml.XmlException or SocketException or UriFormatException)
            {
                // A device that answered the search but not its description: the next answer may be the router.
            }
        }

        return null;
    }

    /// <summary>
    /// Looks for the router without mapping anything (the probe's check of the search): its WAN service's control
    /// address and this machine's address toward it, or why none was found.
    /// </summary>
    public static string FindRouter()
    {
        try
        {
            return Discover(TimeSpan.FromSeconds(2)) is { } gateway
                ? $"router found: {gateway.ServiceType} at {gateway.Control}, this machine {gateway.LocalAddress}"
                : "no UPnP router answered";
        }
        catch (SocketException e)
        {
            return "the search could not be sent: " + e.Message;
        }
    }

    private static string? Header(string answer, string name)
    {
        foreach (string line in answer.Split("\r\n"))
        {
            int colon = line.IndexOf(':', StringComparison.Ordinal);
            if (colon > 0 && line.AsSpan(0, colon).Trim().Equals(name, StringComparison.OrdinalIgnoreCase))
            {
                return line[(colon + 1)..].Trim();
            }
        }

        return null;
    }

    /// <summary>Reads the router's description: the WAN connection service's control address, and this machine's address toward it.</summary>
    private static Gateway? Describe(Uri location)
    {
        using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(3) };
        XDocument doc = XDocument.Parse(http.GetStringAsync(location).GetAwaiter().GetResult());
        XNamespace ns = doc.Root?.Name.Namespace ?? XNamespace.None;
        Uri baseUri = doc.Root?.Element(ns + "URLBase")?.Value is { Length: > 0 } b && Uri.TryCreate(b, UriKind.Absolute, out Uri? u) ? u : location;
        foreach (string type in ServiceTypes)
        {
            XElement? service = doc.Descendants(ns + "service").FirstOrDefault(s => (string?)s.Element(ns + "serviceType") == type);
            if (service?.Element(ns + "controlURL")?.Value is { Length: > 0 } control)
            {
                return new Gateway(new Uri(baseUri, control), type, LocalAddressToward(location.Host));
            }
        }

        return null;
    }

    /// <summary>The address this machine uses to reach the router (the internal client of the mapping).</summary>
    private static string LocalAddressToward(string host)
    {
        using var probe = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
        probe.Connect(host, 1900);
        return ((IPEndPoint)probe.LocalEndPoint!).Address.ToString();
    }

    /// <summary>A router's WAN connection service.</summary>
    private sealed class Gateway(Uri control, string serviceType, string localAddress)
    {
        public string LocalAddress { get; } = localAddress;

        public Uri Control { get; } = control;

        public string ServiceType { get; } = serviceType;

        /// <summary>Runs an action without arguments and returns the value named <paramref name="result"/> of its answer, or null.</summary>
        public string? Query(string action, string result) => Call(action, string.Empty, result, out _, out _);

        /// <summary>Runs one SOAP action. True when it worked; a fault's text comes back in <paramref name="fault"/>.</summary>
        public bool Call(string action, string arguments, out string? fault)
        {
            Call(action, arguments, null, out fault, out bool ok);
            return ok;
        }

        private string? Call(string action, string arguments, string? result, out string? fault, out bool ok)
        {
            string body =
                "<?xml version=\"1.0\"?>" +
                "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">" +
                $"<s:Body><u:{action} xmlns:u=\"{ServiceType}\">{arguments}</u:{action}></s:Body></s:Envelope>";
            using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
            using var request = new HttpRequestMessage(HttpMethod.Post, Control)
            {
                Content = new StringContent(body, Encoding.UTF8, "text/xml"),
            };
            request.Content.Headers.ContentType!.CharSet = "\"utf-8\"";
            request.Headers.TryAddWithoutValidation("SOAPAction", $"\"{ServiceType}#{action}\"");
            using HttpResponseMessage response = http.Send(request, CancellationToken.None);
            string text = response.Content.ReadAsStringAsync().GetAwaiter().GetResult();
            ok = response.IsSuccessStatusCode;
            fault = null;
            if (!ok)
            {
                // A UPnP fault names its code and text (718 ConflictInMappingEntry, 725 OnlyPermanentLeasesSupported...).
                try
                {
                    XDocument f = XDocument.Parse(text);
                    string? code = f.Descendants().FirstOrDefault(e => e.Name.LocalName == "errorCode")?.Value;
                    string? what = f.Descendants().FirstOrDefault(e => e.Name.LocalName == "errorDescription")?.Value;
                    fault = code is null ? $"HTTP {(int)response.StatusCode}" : $"{code} {what}".Trim();
                }
                catch (System.Xml.XmlException)
                {
                    fault = $"HTTP {(int)response.StatusCode}";
                }

                return null;
            }

            if (result is null)
            {
                return null;
            }

            try
            {
                return XDocument.Parse(text).Descendants().FirstOrDefault(e => e.Name.LocalName == result)?.Value;
            }
            catch (System.Xml.XmlException)
            {
                return null;
            }
        }
    }
}
