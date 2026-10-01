# GxEPD2_750c_86BF

GxEPD2 driver for the DKE DEPG0750RWF86BF30 (FPC-8612) 800x480
black/white/red panel: `GxEPD2_750c_Z08` with the panel's own init
sequence instead of the stock one.

Vendored from https://github.com/lumixen/gxepd2-rwf86bf (commit b7acb18,
MIT, see LICENSE), with one debug `Serial.println` removed. Used here for
the closely related DEPG0750RWU790F30HP, which comes out greyish with the
stock Z08 init (see `include/display_config.h`).
