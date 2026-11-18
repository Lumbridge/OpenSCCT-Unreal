"""Cook the original receiver rules onto individual level actors, including mesh instances sharing an asset."""


def receiver_tags(flags, names):
    tags = []
    if flags & 1:
        tags.append("ScctSkipStaticSpot")
    if flags & 2:
        tags.append("ScctSkipDynamicSpot")
    tags.extend("ScctSkipSpot:" + name.rsplit(".", 1)[-1] for name in names if isinstance(name, str) and name)
    return tags


def gameplay_receiver_tags(props):
    flags = 3 if props.get("GlassEffect") else (
        int(bool(props.get("bIgnoreEchelonLighting"))) | (int(bool(props.get("bIgnoreDynamicEchelonLighting"))) << 1))
    names = (ref.get("$actor") or ref.get("$asset") for ref in props.get("IgnoredEchelonLights", []) if isinstance(ref, dict))
    return receiver_tags(flags, names)
