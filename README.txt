Saved Places to KMZ
===================
Build and run (a C11 compiler and make are needed):

    make
    ./takeout2kmz "Saved Places.json" places.kmz

Linux installation (defaults to /usr/local/bin):
    make
    sudo make install
    takeout2kmz "Saved Places.json" places.kmz
    sudo make uninstall

Per-user installation (no sudo; add ~/.local/bin to PATH if needed):
    make install prefix="$HOME/.local"
    make uninstall prefix="$HOME/.local"

Packaging/staging: make install prefix=/usr DESTDIR="$PWD/stage"
This places the binary in stage/usr/bin. Uninstall supports the same
prefix, exec_prefix, bindir, and DESTDIR overrides and leaves directories.
CC, CPPFLAGS, CFLAGS, LDFLAGS, and LDLIBS can be overridden on make's command
line. Run make clean to remove the locally built executable.

Or compile directly:
    cc -std=c11 -O2 -Wall -Wextra -Wpedantic main.c -o takeout2kmz

No external libraries, commands, network access, or compression library are
used by the program. It writes doc.kml inside a standard ZIP archive using
the stored (uncompressed) method allowed by the KMZ specification.
The install target uses the usual system install, mkdir, and rm utilities.

Input: Google Takeout Saved Places.json, a GeoJSON FeatureCollection with
Point features. Coordinates are longitude, latitude, optionally altitude
(altitude is ignored). If point coordinates are unavailable or invalid,
Location / Geo Coordinates / Longitude and Latitude are used instead.
Names, addresses, Maps URLs, notes, comments, descriptions, country codes,
and published/updated fields are retained when present as strings.

Import the resulting KMZ into your mapping application's KML/KMZ importer.
Unusable entries are reported by their 1-based feature index and skipped.
Exit status: 0 = complete, 2 = KMZ written with skipped entries, 1 = error.
If every entry is unusable, no file is created. An empty collection is valid.
Existing output files are never overwritten; choose a new output filename.

CSV saved lists and place IDs without coordinates cannot be converted.
Input and KML are held in memory; JSON nesting is limited to 128 levels and
the archive must fit classic ZIP's roughly 4 GiB size limit. JSON controls
that XML 1.0 cannot represent are replaced with the replacement character.

Format references:
https://github.com/purarue/google_takeout_parser/issues/20
https://developers.google.com/kml/documentation/kmzarchives
https://docs.ogc.org/is/12-007r2/12-007r2.html (Annex C: stored ZIP is allowed)
