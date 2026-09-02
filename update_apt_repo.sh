#!/bin/bash
# update_apt_repo.sh - Build and deploy GitHub Pages APT repository and Download Portal
# Modeled after taskmgr project

set -e

PROJECT_NAME="tuneperfs"
GITHUB_REPO="https://github.com/seb3773/tuneperf.git"
PAGES_BRANCH="gh-pages"

echo "=================================================="
echo " Building APT Repository and Download Portal"
echo "=================================================="

# Check for apt-ftparchive
if ! command -v apt-ftparchive >/dev/null 2>&1; then
    echo "Error: apt-ftparchive is required. Install apt-utils."
    exit 1
fi

# Find latest packages
LATEST_DEB=$(ls -1 tuneperfs-gui_*_amd64.deb 2>/dev/null | grep -v static | sort -V | tail -n 1)
LATEST_STATIC_DEB=$(ls -1 tuneperfs-gui_*_static.deb 2>/dev/null | sort -V | tail -n 1)
LATEST_TAR=$(ls -1 tuneperfs-gui_*_amd64.tar.gz 2>/dev/null | grep -v static | sort -V | tail -n 1)
LATEST_STATIC_TAR=$(ls -1 tuneperfs-gui_*_static.tar.gz 2>/dev/null | sort -V | tail -n 1)
LATEST_APPIMAGE=$(ls -1 tuneperfs-gui-*.AppImage 2>/dev/null | sort -V | tail -n 1)
LATEST_QSI=$(ls -1 setup_tuneperfs-gui_*.qsi 2>/dev/null | sort -V | tail -n 1)

if [ -z "$LATEST_DEB" ]; then
    echo "Error: No .deb packages found!"
    exit 1
fi

# Create a temporary directory for gh-pages clone
PAGES_DIR=$(mktemp -d)
echo "Cloning gh-pages branch into temporary directory..."
git clone --branch "$PAGES_BRANCH" "$GITHUB_REPO" "$PAGES_DIR" || {
    echo "Branch $PAGES_BRANCH does not exist. Creating it as an orphan..."
    git clone "$GITHUB_REPO" "$PAGES_DIR"
    cd "$PAGES_DIR"
    git checkout --orphan "$PAGES_BRANCH"
    git rm -rf .
    cd -
}

# Create repository structure
REPO_POOL_DIR="$PAGES_DIR/pool/main/t/$PROJECT_NAME"
mkdir -p "$REPO_POOL_DIR"
mkdir -p "$PAGES_DIR/screenshots"

# Prevent Jekyll processing (required for folders starting with underscore, etc.)
touch "$PAGES_DIR/.nojekyll"

# Copy binary packages to gh-pages
echo "Copying latest packages..."
cp "$LATEST_DEB" "$REPO_POOL_DIR/"
[ -n "$LATEST_STATIC_DEB" ] && cp "$LATEST_STATIC_DEB" "$REPO_POOL_DIR/"
[ -n "$LATEST_APPIMAGE" ] && cp "$LATEST_APPIMAGE" "$PAGES_DIR/"
[ -n "$LATEST_QSI" ] && cp "$LATEST_QSI" "$PAGES_DIR/"
[ -n "$LATEST_TAR" ] && cp "$LATEST_TAR" "$PAGES_DIR/"
[ -n "$LATEST_STATIC_TAR" ] && cp "$LATEST_STATIC_TAR" "$PAGES_DIR/"

# Copy assets
echo "Copying assets..."
[ -f "konqi_perfs.jpg" ] && cp "konqi_perfs.jpg" "$PAGES_DIR/"
[ -f "icons/tuneperfs.png" ] && cp "icons/tuneperfs.png" "$PAGES_DIR/favicon.png"
if [ -d "screenshots" ]; then
    # Copy up to 8 screenshots for the grid
    ls -1 screenshots/screenshot_tuneperf_*.jpg 2>/dev/null | head -n 8 | xargs -I {} cp {} "$PAGES_DIR/screenshots/"
fi

# Generate Packages file
echo "Generating Packages and Release files..."
cd "$PAGES_DIR"
# The Packages file needs paths relative to the repository root
apt-ftparchive packages pool/ > Packages
gzip -k -f Packages

# Generate Release file
apt-ftparchive release . > Release

# Extract just filenames for HTML
LATEST_DEB_NAME=$(basename "$LATEST_DEB")
LATEST_STATIC_DEB_NAME=$(basename "$LATEST_STATIC_DEB")
LATEST_APPIMAGE_NAME=$(basename "$LATEST_APPIMAGE")
LATEST_QSI_NAME=$(basename "$LATEST_QSI")
LATEST_TAR_NAME=$(basename "$LATEST_TAR")
LATEST_STATIC_TAR_NAME=$(basename "$LATEST_STATIC_TAR")

# Extract version from deb filename (e.g. tuneperfs-gui_1.0_amd64.deb -> 1.0)
VERSION=$(echo "$LATEST_DEB_NAME" | cut -d'_' -f2)

# Generate HTML Portal
echo "Generating index.html..."
cat << EOF > index.html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>TunePerf - System Optimizer</title>
  <link rel="icon" type="image/png" href="favicon.png">
  <style>
    :root {
      --bg: #0f172a;
      --surface: #1e293b;
      --surface-hover: #334155;
      --text: #f8fafc;
      --text-muted: #94a3b8;
      --primary: #38bdf8;
      --primary-hover: #0284c7;
      --radius: 12px;
      --border: 1px solid #334155;
    }

    * { box-sizing: border-box; margin: 0; padding: 0; }

    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
      background-color: var(--bg);
      color: var(--text);
      line-height: 1.6;
      -webkit-font-smoothing: antialiased;
    }

    .container { max-width: 1000px; margin: 0 auto; padding: 40px 20px; }

    header {
      text-align: center;
      margin-bottom: 40px;
    }

    .logo {
      width: 110px;
      height: 110px;
      margin-bottom: 16px;
      border-radius: 50%; /* Just in case it's not a transparent png */
      filter: drop-shadow(0 8px 24px rgba(58, 134, 255, 0.45));
      transition: transform 0.3s cubic-bezier(0.34, 1.56, 0.64, 1);
    }

    .logo:hover {
      transform: scale(1.08) rotate(3deg);
    }

    .badge {
      display: inline-block;
      padding: 4px 14px;
      font-size: 0.85rem;
      font-weight: 600;
      color: #fff;
      background: linear-gradient(135deg, #0ea5e9, #3b82f6);
      border-radius: 20px;
      margin-bottom: 12px;
      text-transform: uppercase;
      letter-spacing: 0.5px;
      margin-right: 6px;
    }

    .version-pill {
      display: inline-block;
      font-size: 1.1rem;
      font-weight: 600;
      color: #38bdf8;
      background: rgba(56, 189, 248, 0.12);
      border: 1px solid rgba(56, 189, 248, 0.35);
      padding: 2px 12px;
      border-radius: 20px;
      vertical-align: middle;
      margin-left: 8px;
    }

    h1 {
      font-size: 2.4rem;
      font-weight: 700;
      margin-bottom: 8px;
      display: flex;
      align-items: center;
      justify-content: center;
    }

    p.lead {
      font-size: 1.1rem;
      color: var(--text-muted);
      max-width: 680px;
      margin: 0 auto;
    }

    .card {
      background: var(--surface); border: var(--border); border-radius: var(--radius);
      padding: 30px; margin-bottom: 30px; box-shadow: 0 4px 6px rgba(0,0,0,0.1);
    }

    h2 { font-size: 1.5rem; margin-bottom: 20px; display: flex; align-items: center; gap: 10px; border-bottom: 2px solid var(--primary); padding-bottom: 10px; display: inline-flex;}

    /* APT Section */
    .code-block {
      background: #000; padding: 15px; border-radius: 8px; font-family: monospace;
      color: #10b981; overflow-x: auto; margin: 15px 0; font-size: 0.95rem; border: 1px solid #333;
    }
    .btn-copy {
      background: var(--surface-hover); color: var(--text); border: none; padding: 8px 16px;
      border-radius: 6px; cursor: pointer; font-weight: 600; font-size: 0.9rem; transition: background 0.2s;
    }
    .btn-copy:hover { background: #475569; }

    /* Downloads Grid */
    .downloads-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 20px; }
    .download-card {
      background: #151e2e; border: var(--border); border-radius: 8px; padding: 20px;
      display: flex; flex-direction: column;
    }
    .download-header { display: flex; justify-content: space-between; align-items: flex-start; margin-bottom: 10px; }
    .download-title { font-weight: 600; font-size: 1.1rem; }
    .download-tag { background: rgba(56, 189, 248, 0.1); color: var(--primary); padding: 4px 8px; border-radius: 20px; font-size: 0.75rem; font-weight: 700; text-transform: uppercase; letter-spacing: 0.5px;}
    .download-desc { color: var(--text-muted); font-size: 0.9rem; margin-bottom: 20px; flex-grow: 1; }
    .btn-download {
      display: block; width: 100%; text-align: center; background: var(--primary); color: #fff;
      padding: 10px; border-radius: 6px; text-decoration: none; font-weight: 600; transition: background 0.2s;
    }
    .btn-download:hover { background: var(--primary-hover); }

    /* Features Grid */
    .features-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 20px; }
    .feature-item { background: #151e2e; padding: 20px; border-radius: 8px; border: var(--border); }
    .feature-icon { font-size: 1.8rem; margin-bottom: 10px; display: block; }
    .feature-title { font-weight: 600; margin-bottom: 8px; font-size: 1.05rem;}
    .feature-text { color: var(--text-muted); font-size: 0.9rem; }

    /* Screenshots Grid */
    .screenshots-grid { display: grid; grid-template-columns: repeat(4, 1fr); gap: 15px; }
    @media (max-width: 768px) { .screenshots-grid { grid-template-columns: repeat(2, 1fr); } }
    .screenshot-thumb {
      aspect-ratio: 16/10; border-radius: 8px; overflow: hidden; cursor: pointer; border: var(--border);
      transition: transform 0.2s, box-shadow 0.2s;
    }
    .screenshot-thumb:hover { transform: translateY(-3px); box-shadow: 0 8px 15px rgba(0,0,0,0.3); }
    .screenshot-thumb img { width: 100%; height: 100%; object-fit: cover; }

    /* Modal Lightbox */
    .modal {
      display: none; position: fixed; z-index: 1000; left: 0; top: 0; width: 100%; height: 100%;
      background-color: rgba(0,0,0,0.9); justify-content: center; align-items: center; padding: 20px;
    }
    .modal.active { display: flex; }
    .modal img { max-width: 90vw; max-height: 90vh; border-radius: 8px; box-shadow: 0 0 20px rgba(0,0,0,0.5); }
    .modal-close { position: absolute; top: 20px; right: 30px; color: #fff; font-size: 40px; font-weight: bold; cursor: pointer; }

    footer { text-align: center; margin-top: 50px; padding-top: 20px; border-top: var(--border); color: var(--text-muted); font-size: 0.9rem; }
    a { color: var(--primary); text-decoration: none; }
    a:hover { text-decoration: underline; }
    .footer-links { margin-top: 10px; display: flex; justify-content: center; gap: 15px; flex-wrap: wrap; }
  </style>
</head>
<body>

  <div class="container">
    <header>
      <img src="konqi_perfs.jpg" alt="TunePerf Logo" class="logo" onerror="this.style.display='none'">
      <br>
      <span class="badge">Official APT Repository</span>
      <span class="badge" style="background: linear-gradient(135deg, #10b981, #059669);">TDE &amp; Linux Native</span>
      <span class="badge" style="background: linear-gradient(135deg, #8b5cf6, #6d28d9);">x86_64</span>
      <h1>TunePerf <span class="version-pill">v${VERSION}</span></h1>
      <p class="lead">Intelligent Linux System Optimizer GUI</p>
    </header>

    <!-- Method 1: APT Repository -->
    <div class="card">
      <h2>
        <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="#38bdf8" stroke-width="2"><path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"></path><polyline points="3.27 6.96 12 12.01 20.73 6.96"></polyline><line x1="12" y1="22.08" x2="12" y2="12"></line></svg>
        Method 1: APT Repository (Recommended)
      </h2>
      <p style="color: var(--text-muted);">Install via the official APT repository for automatic seamless updates on Debian/Ubuntu/Trinity based systems.</p>
      
      <div class="code-block" id="apt-code">echo "deb [trusted=yes] https://seb3773.github.io/tuneperf/ stable main" | sudo tee /etc/apt/sources.list.d/tuneperfs.list &amp;&amp; sudo apt update &amp;&amp; sudo apt install tuneperfs-gui</div>
      <button class="btn-copy" onclick="copyCode('apt-code', this)">Copy Command</button>
    </div>

    <!-- Method 2: Direct Downloads -->
    <div class="card">
      <h2>
        <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="#38bdf8" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="7 10 12 15 17 10"></polyline><line x1="12" y1="15" x2="12" y2="3"></line></svg>
        Method 2: Direct Downloads
      </h2>
      <p style="color: var(--text-muted); margin-bottom: 20px;">Download the package format that best suits your distribution.</p>
      
      <div class="downloads-grid">
        <div class="download-card">
          <div class="download-header">
            <span class="download-title">Debian Package (.deb)</span>
            <span class="download-tag">Native</span>
          </div>
          <p class="download-desc">Debian/Ubuntu native installers. Available in dynamic (system Qt) or static standalone build.</p>
          <a href="pool/main/t/tuneperfs/${LATEST_DEB_NAME}" class="btn-download" style="margin-bottom: 8px;">
            Download Dynamic .deb
          </a>
          <a href="pool/main/t/tuneperfs/${LATEST_STATIC_DEB_NAME}" class="btn-download" style="background: var(--surface); border: 1px solid var(--primary); color: var(--primary);">
            Download Static .deb
          </a>
        </div>

        <div class="download-card">
          <div class="download-header">
            <span class="download-title">Q4OS Installer (.qsi)</span>
            <span class="download-tag">Q4OS 1-Click</span>
          </div>
          <p class="download-desc">Graphical one-click installer designed specifically for Q4OS Trinity desktop. Automatically configures APT.</p>
          <a href="${LATEST_QSI_NAME}" class="btn-download">
            Download .qsi
          </a>
        </div>

        <div class="download-card">
          <div class="download-header">
            <span class="download-title">Standalone AppImage</span>
            <span class="download-tag">Universal x86_64</span>
          </div>
          <p class="download-desc">Self-contained portable executable with bundled dependencies (runs on any distro).</p>
          <a href="${LATEST_APPIMAGE_NAME}" class="btn-download">
            Download AppImage
          </a>
        </div>

        <div class="download-card">
          <div class="download-header">
            <span class="download-title">Portable Tarball (.tar.gz)</span>
            <span class="download-tag">Portable</span>
          </div>
          <p class="download-desc">Compressed binaries for portable execution without installation.</p>
          <a href="${LATEST_TAR_NAME}" class="btn-download" style="margin-bottom: 8px;">
            Download Dynamic .tar.gz
          </a>
          <a href="${LATEST_STATIC_TAR_NAME}" class="btn-download" style="background: var(--surface); border: 1px solid var(--primary); color: var(--primary);">
            Download Static .tar.gz
          </a>
        </div>
      </div>
    </div>

    <!-- Key Features -->
    <div class="card">
      <h2>
        <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="#38bdf8" stroke-width="2"><path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"/></svg>
        Key Capabilities &amp; Architecture
      </h2>

      <div class="features-grid">
        <div class="feature-item">
          <span class="feature-icon">🧠</span>
          <div class="feature-title">Smart Topography Analysis</div>
          <div class="feature-text">Dynamically parses CPU threads, hardware cache, architecture (x86_64, NVMe, GPUs) to select best profiles.</div>
        </div>

        <div class="feature-item">
          <span class="feature-icon">⚙️</span>
          <div class="feature-title">Kernel & Scheduler Tweaks</div>
          <div class="feature-text">Fine-tunes completely transparently EEVDF, CFS, swappiness, THP rules, and block I/O schedulers.</div>
        </div>

        <div class="feature-item">
          <span class="feature-icon">🌐</span>
          <div class="feature-title">Network TCP/IP Boost</div>
          <div class="feature-text">Reconfigures TCP window scaling, queue lengths, BBR congestion algorithms, and offloading automatically.</div>
        </div>

        <div class="feature-item">
          <span class="feature-icon">🔋</span>
          <div class="feature-title">Hibernation & Swap Management</div>
          <div class="feature-text">Diagnoses and provisions safe, BTRFS-compatible Swapfiles and configures GRUB for resume-from-disk.</div>
        </div>

        <div class="feature-item">
          <span class="feature-icon">🛡️</span>
          <div class="feature-title">Security & Mitigations</div>
          <div class="feature-text">Offers an extreme performance switch to disable speculative CPU security mitigations (Spectre/Meltdown).</div>
        </div>

        <div class="feature-item">
          <span class="feature-icon">🚀</span>
          <div class="feature-title">Non-intrusive Architecture</div>
          <div class="feature-text">Runs as a one-shot payload at boot. Configures via simple sysctl / sysfs injections without persistent daemons.</div>
        </div>
      </div>
    </div>

    <!-- Screenshots -->
    <div class="card">
      <h2>
        <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="#38bdf8" stroke-width="2"><rect x="3" y="3" width="18" height="18" rx="2" ry="2"/><circle cx="8.5" cy="8.5" r="1.5"/><polyline points="21 15 16 10 5 21"/></svg>
        Screenshots
      </h2>
      <div class="screenshots-grid">
EOF

# Inject screenshots dynamically
ls -1 screenshots/screenshot_tuneperf_*.jpg 2>/dev/null | head -n 8 | while read -r img; do
    filename=$(basename "$img")
    cat << HTML_EOF >> index.html
        <div class="screenshot-thumb" onclick="openModal('screenshots/$filename')">
          <img src="screenshots/$filename" alt="TunePerf Screenshot">
        </div>
HTML_EOF
done

cat << EOF >> index.html
      </div>
    </div>

    <!-- Footer -->
    <footer>
      <p>Source Code &amp; Releases: <a href="https://github.com/seb3773/tuneperf" target="_blank" rel="noopener">github.com/seb3773/tuneperf</a></p>
      <p style="margin-top: 6px;">Developed with ❤️ for the Linux and Trinity Desktop Environment community.</p>
      <p class="footer-links">
        <a href="http://trinitydesktop.org/" target="_blank" rel="noopener">http://trinitydesktop.org/</a> &bull; 
        <a href="https://www.q4os.org/" target="_blank" rel="noopener">https://www.q4os.org/</a>
      </p>
    </footer>

  </div>

  <!-- Lightbox Modal -->
  <div id="imageModal" class="modal" onclick="closeModal()">
    <span class="modal-close">&times;</span>
    <img id="modalImg" src="" alt="Enlarged screenshot" onclick="event.stopPropagation()">
  </div>

  <script>
    function copyCode(id, btn) {
      const text = document.getElementById(id).innerText;
      navigator.clipboard.writeText(text).then(() => {
        const orig = btn.innerText;
        btn.innerText = "Copied!";
        setTimeout(() => btn.innerText = orig, 2000);
      });
    }

    function openModal(src) {
      document.getElementById('modalImg').src = src;
      document.getElementById('imageModal').classList.add('active');
    }

    function closeModal() {
      document.getElementById('imageModal').classList.remove('active');
    }

    document.addEventListener('keydown', (e) => {
      if (e.key === 'Escape') closeModal();
    });
  </script>
</body>
</html>
EOF

# Git commit and push to gh-pages
echo "Committing and pushing to gh-pages branch..."
(
    cd "$PAGES_DIR"
    git add -A
    git commit -m "Update APT repository and remodel page: $(date +'%Y-%m-%d %H:%M:%S')" || echo "No changes to commit."
    git push origin "$PAGES_BRANCH"
)

cp "$PAGES_DIR/index.html" /home/cdef/_PROJETS/tuneperfs/index.html

echo "Cleaning up temporary directory..."
rm -rf "$PAGES_DIR"

echo "=================================================="
echo " SUCCESS: APT repository updated on gh-pages!"
echo " URL: https://seb3773.github.io/tuneperf/"
echo "=================================================="
