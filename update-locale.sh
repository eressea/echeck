#!/bin/sh
xgettext --no-location -k_ -kt -d echeck -o po/echeck.pot -s echeck.c
msgmerge -N -o po/echeck.de.po po/echeck.de.po po/echeck.pot
mkdir -p locale/de/LC_MESSAGES
msgfmt -f -c po/echeck.de.po -o locale/de/LC_MESSAGES/echeck.mo

