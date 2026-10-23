#!/bin/sh
# Install CBMC on an Ubuntu runner: the newest release .deb for this Ubuntu version from
# github.com/diffblue/cbmc, falling back to the distribution package (older, MiniSAT only).
# Prints the version and which solver back ends this build offers.
set -u
ver=$(. /etc/os-release && echo "$VERSION_ID")
url=$(curl -fsSL ${GITHUB_TOKEN:+-H "Authorization: Bearer $GITHUB_TOKEN"} \
        https://api.github.com/repos/diffblue/cbmc/releases/latest 2>/dev/null |
      sed -n 's/.*"browser_download_url": *"\([^"]*ubuntu-'"$ver"'-cbmc-[^"]*\.deb\)".*/\1/p' | head -n 1)
installed=0
if [ -n "$url" ]; then
    echo "installing $url"
    if curl -fsSL -o /tmp/cbmc.deb "$url" && sudo apt-get install -y /tmp/cbmc.deb; then
        installed=1
    fi
fi
if [ "$installed" -eq 0 ]; then
    echo "release package not available, using the distribution package"
    sudo apt-get update && sudo apt-get install -y cbmc || exit 1
fi
cbmc --version
echo "solver options offered:"
cbmc --help 2>&1 | grep -Eo -- '--(cadical|glucose|kissat|minisat2|bitwuzla|z3|cvc5|yices|boolector|smt2)\b' | sort -u | tr '\n' ' '
echo
# readable without opening the log: annotations are served by the checks API
{
    echo "cbmc: $(cbmc --version 2>&1 | head -n 1), package: $([ "$installed" -eq 1 ] && echo "release $url" || echo distribution)"
    echo "options mentioning solvers:"
    cbmc --help 2>&1 | grep -iE 'sat|solver|cadical|glucose|smt' | head -n 15
} | sed -e 's/%/%25/g' | awk '{ printf "%s%%0A", $0 }' | { read -r msg; echo "::notice title=cbmc build::$msg"; }

