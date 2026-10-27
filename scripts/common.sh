set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GAME_DIR="$ROOT/game"
ELF="$GAME_DIR/SLPS_255.45"
CONFIG="$ROOT/recomp/ffop.toml"
GENERATED="$ROOT/generated"
TOOLS_BUILD="$ROOT/build/tools"
GAME_BUILD="$ROOT/build/game"
JOBS="${JOBS:-$(nproc)}"
