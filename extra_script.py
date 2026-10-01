Import("env")

import os

# The art partition (partitions.csv) and what goes in it.
ART_OFFSET = "0x330000"
ART_BIN = os.path.join(env.subst("$PROJECT_DIR"), "art", "art.bin")

# A USB upload writes the artworks too, next to the bootloader and
# partition table, so a freshly flashed board is complete. OTA uploads
# only carry the app.
env.Append(FLASH_EXTRA_IMAGES=[(ART_OFFSET, ART_BIN)])


# `pio run -t uploadart`: only the artworks, after tools/convert_art.py.
# Over USB with esptool, or with the OTA env (espota's --spiffs mode
# writes the first spiffs-subtype partition, which is the art one).
def upload_art_cmd():
    if env.subst("$UPLOAD_PROTOCOL") == "espota":
        return '"$PYTHONEXE" "$UPLOADER" $UPLOADERFLAGS --spiffs -f "%s"' % ART_BIN
    return ('$UPLOADER --chip esp32 --port "$UPLOAD_PORT" --baud $UPLOAD_SPEED '
            'write-flash -z %s "%s"' % (ART_OFFSET, ART_BIN))


def upload_art(source, target, env):
    env.AutodetectUploadPort()
    return env.Execute(env.VerboseAction(upload_art_cmd(), "Uploading art/art.bin"))


env.AddCustomTarget(
    name="uploadart",
    dependencies=None,
    actions=[upload_art],
    title="Upload artworks",
    description="Write art/art.bin to the art partition (USB or OTA)",
)
