using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.UnrealBridge.Test;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The entry points of the TestRange drills (vision-cancelling, spy-grab). Unreal asks for
/// <see cref="LessonExports.GetApi"/> once (type "OpenScct.UnrealBridge.LessonExports, OpenScct.UnrealBridge", method
/// "GetApi") and keeps the table. Source/OpenSCCT/Test/ScctLessonApi.h mirrors it field for field.
/// <para>A drill is started in two steps: <c>Arm</c> before the level starts its local match (the start then becomes
/// the drill's: TestRange, spy, Play Level rules, inline server, the drill's bots and start, Test/LessonLaunch.cs),
/// then <c>StartLesson</c> once that match plays. No entry throws: a failure returns 0 with the reason in
/// <c>LastError</c>, and leaves the session running.</para>
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct LessonApi
{
    public int Version;

    /// <summary>Arms a drill for the next local start on TestRange; null or "" disarms. Returns 1, 0 for an unknown kind.</summary>
    public delegate* unmanaged[Cdecl]<char*, int> Arm;

    /// <summary>Starts the drill on the current session. Returns 1, or 0 with the reason in <c>LastError</c>.</summary>
    public delegate* unmanaged[Cdecl]<char*, int> StartLesson;

    /// <summary>Ends the drill of the current session (the match goes on). Returns 1 when one ran.</summary>
    public delegate* unmanaged[Cdecl]<int> StopLesson;

    /// <summary>Writes the coach state (<see cref="BridgeLessonState"/>). Returns 1 while a drill runs, else 0 (state zeroed).</summary>
    public delegate* unmanaged[Cdecl]<BridgeLessonState*, int> GetState;

    /// <summary>Writes the recent attempts, oldest first. Returns the count written.</summary>
    public delegate* unmanaged[Cdecl]<BridgeLessonAttempt*, int, int> GetAttempts;

    /// <summary>
    /// One of the lesson's actions: "reset" (vision-cancelling: back to the ledge), "moving" (spy-grab: the target
    /// walks or stops), "debug", and the browser lesson's own "forward", "back", "stop", "night", "thermal", "grab".
    /// Returns 1 when a drill took it.
    /// </summary>
    public delegate* unmanaged[Cdecl]<char*, int> Action;

    /// <summary>Copies the last failure's text. Returns its length.</summary>
    public delegate* unmanaged[Cdecl]<char*, int, int> LastError;
}

public static unsafe class LessonExports
{
    public const int ApiVersion = 1;

    private static string s_lastError = "";

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(LessonApi* api)
    {
        if (api == null)
        {
            return 0;
        }

        api->Version = ApiVersion;
        api->Arm = &Arm;
        api->StartLesson = &StartLesson;
        api->StopLesson = &StopLesson;
        api->GetState = &GetState;
        api->GetAttempts = &GetAttempts;
        api->Action = &Action;
        api->LastError = &LastError;
        return ApiVersion;
    }

    private static string? Text(char* text) => text == null || *text == '\0' ? null : new string(text);

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Arm(char* kind)
    {
        try
        {
            string? name = Text(kind);
            if (name is not null && !LessonLaunch.IsKind(name))
            {
                s_lastError = $"There is no drill '{name}' (vision-cancelling, spy-grab).";
                return 0;
            }

            LessonLaunch.Arm(name);
            return 1;
        }
        catch (Exception e)
        {
            s_lastError = e.Message;
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StartLesson(char* kind)
    {
        try
        {
            string? name = Text(kind);
            if (!LessonLaunch.IsKind(name))
            {
                s_lastError = $"There is no drill '{name}' (vision-cancelling, spy-grab).";
                return 0;
            }

            if (Exports.Current is not { } session)
            {
                s_lastError = "No match is running.";
                return 0;
            }

            session.Feature(s => new LessonFeature(s)).Start(name!);
            return 1;
        }
        catch (Exception e)
        {
            s_lastError = e.Message;
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StopLesson()
    {
        try
        {
            if (Exports.Current is not { } session || session.Feature(s => new LessonFeature(s)) is not { Lesson: not null } feature)
            {
                return 0;
            }

            feature.Stop();
            return 1;
        }
        catch (Exception e)
        {
            s_lastError = e.Message;
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetState(BridgeLessonState* state)
    {
        if (state == null)
        {
            return 0;
        }

        try
        {
            *state = default;
            if (Exports.Current is not { IsDisposed: false } session)
            {
                return 0;
            }

            session.Feature(s => new LessonFeature(s)).Read(ref *state);
            return state->Active;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetAttempts(BridgeLessonAttempt* attempts, int capacity)
    {
        if (attempts == null || capacity <= 0 || Exports.Current is not { IsDisposed: false } session)
        {
            return 0;
        }

        try
        {
            return session.Feature(s => new LessonFeature(s)).ReadAttempts(new Span<BridgeLessonAttempt>(attempts, capacity));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Action(char* action)
    {
        try
        {
            string? name = Text(action);
            if (name is null || Exports.Current is not { IsDisposed: false } session ||
                session.Feature(s => new LessonFeature(s)).Lesson is not { } lesson)
            {
                return 0;
            }

            lesson.Action(name);
            return 1;
        }
        catch (Exception e)
        {
            s_lastError = e.Message;
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int LastError(char* buffer, int capacity)
    {
        string text = s_lastError;
        if (buffer != null && capacity > 0)
        {
            int length = Math.Min(text.Length, capacity - 1);
            text.AsSpan(0, length).CopyTo(new Span<char>(buffer, length));
            buffer[length] = '\0';
        }

        return text.Length;
    }
}
