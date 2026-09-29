Import("env")


def force_uncompressed_upload(source, target, env):
    # This platform's builder hardcodes "-z" (compress) right after
    # "write-flash" with no regard for --no-stub, but --no-stub mode
    # needs uncompressed writes or the ROM bootloader rejects the data
    # ("format of the received message is invalid"). Swap it for "-u"
    # (no-compress) in the same slot right before upload runs.
    flags = env.get("UPLOADERFLAGS", [])
    if "-z" in flags:
        flags[flags.index("-z")] = "-u"
        env.Replace(UPLOADERFLAGS=flags)


env.AddPreAction("upload", force_uncompressed_upload)
