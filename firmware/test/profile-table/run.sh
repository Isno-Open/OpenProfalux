#!/bin/sh
# Banc hote de profile_table.c contre la table engendree depuis le vrai profiles.json.
#   firmware/test/profile-table/run.sh
set -e
here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../../.."
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
python3 "$root/tools/gen_profiles.py" "$root/firmware/main/web/profiles.json" "$out/profiles_gen.c"
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$root/firmware/main" \
   "$here/main/test_main.c" "$root/firmware/main/profile_table.c" "$root/firmware/main/step_cycle.c" "$out/profiles_gen.c" -o "$out/profile_table_test"
"$out/profile_table_test"
