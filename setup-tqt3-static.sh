#!/bin/bash
################################################
# setup-tqt3-static.sh
# Prépare la lib TQt3 statique dans le projet (libs/tqt3-static/)
# - copie libtqt-mt.a, les headers tq*.h, et tqmoc
# - génère le shim ntq*.h -> tq*.h pour les headers utilisés par le code
#
# Source externe par défaut : /home/cdef/_PROJETS/tqt3
# Override : TQT3_SRC=/chemin/vers/tqt3 bash setup-tqt3-static.sh
################################################
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TQT3_SRC="${TQT3_SRC:-/home/cdef/_PROJETS/tqt3}"
DEST="$PROJECT_DIR/libs/tqt3-static"

# --- Vérifs source ---
if [ ! -f "$TQT3_SRC/lib/libtqt-mt.a" ]; then
    echo "ERREUR: libtqt-mt.a introuvable dans $TQT3_SRC/lib/" >&2
    exit 1
fi
if [ ! -x "$TQT3_SRC/bin/tqmoc" ]; then
    echo "ERREUR: tqmoc introuvable dans $TQT3_SRC/bin/" >&2
    exit 1
fi
if [ ! -d "$TQT3_SRC/include" ]; then
    echo "ERREUR: include/ introuvable dans $TQT3_SRC/" >&2
    exit 1
fi

echo ">>> Source TQt3 statique : $TQT3_SRC"
echo ">>> Destination          : $DEST"

# --- Nettoyage éventuel d'une précédente installation ---
rm -rf "$DEST"
mkdir -p "$DEST/bin" "$DEST/lib" "$DEST/include" "$DEST/ntq-shim"

# --- Copie lib ---
echo ">>> Copie libtqt-mt.a ($(du -h "$TQT3_SRC/lib/libtqt-mt.a" | cut -f1))..."
cp "$TQT3_SRC/lib/libtqt-mt.a" "$DEST/lib/"

# --- Copie headers tq*.h (et private/) ---
# -L : déréférence les symlinks (l'arbre source tqt3 utilise des liens
#       tqxxx.h -> ../src/.../tqxxx.h). On veut de vrais fichiers dans DEST.
echo ">>> Copie des headers..."
cp -rL "$TQT3_SRC/include/." "$DEST/include/"

# --- Copie tqmoc ---
echo ">>> Copie de tqmoc..."
cp "$TQT3_SRC/bin/tqmoc" "$DEST/bin/"

# --- Génération du shim ntq*.h -> tq*.h ---
# On scanne src/ et libs/ pour la liste exacte des ntq*.h inclus,
# puis on crée un wrapper pour chacun.
echo ">>> Génération du shim ntq -> tq..."

mapfile -t NTQ_HEADERS < <(
    grep -rhoE '#include[[:space:]]*[<"][^>"]*ntq[a-z0-9_]+\.h[>"]' \
        "$PROJECT_DIR/src" "$PROJECT_DIR/libs" 2>/dev/null \
      | grep -oE 'ntq[a-z0-9_]+\.h' \
      | sort -u
)

if [ ${#NTQ_HEADERS[@]} -eq 0 ]; then
    echo "AVERTISSEMENT: aucun header ntq* trouvé dans le code." >&2
fi

for ntqh in "${NTQ_HEADERS[@]}"; do
    # ntqxxx.h -> tqxxx.h  (le préfixe ntq devient tq : on remplace le n initial par t)
    tqh="${ntqh/#ntq/tq}"
    if [ ! -f "$DEST/include/$tqh" ]; then
        echo "   AVERTISSEMENT: $tqh absent de la lib statique (référé par $ntqh)" >&2
    fi
    cat > "$DEST/ntq-shim/$ntqh" <<EOF
// Auto-généré par setup-tqt3-static.sh
// Redirige ntq*.h (convention TDE) vers tq*.h (headers natifs TQt3 statiques)
#include <$tqh>
EOF
done

echo ">>> ${#NTQ_HEADERS[@]} headers ntq* redirigés."

# --- .gitignore pour ce dossier (volumineux, reproductible) ---
cat > "$DEST/.gitignore" <<'EOF'
# Généré par setup-tqt3-static.sh — reproductible, ne pas committer
/*
!.gitignore
EOF

echo ""
echo "=== Terminate ==="
echo "  $DEST/lib/libtqt-mt.a"
echo "  $DEST/include/ ($(ls "$DEST/include"/*.h 2>/dev/null | wc -l) headers tq*)"
echo "  $DEST/bin/tqmoc"
echo "  $DEST/ntq-shim/ ($(ls "$DEST/ntq-shim"/*.h 2>/dev/null | wc -l) wrappers ntq*)"
echo ""
echo "Build statique : cmake -S . -B build-static -DSTATIC_BUILD=ON && cmake --build build-static -j"
