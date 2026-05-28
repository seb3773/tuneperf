#!/usr/bin/env bash
# tuneperf.sh - Intelligent System Tuning based on Hardware and Usage Profile

set -euo pipefail
IFS=$'\n\t'

# --- Utils & Colors ---
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo -e "${RED}Missing required command: $1${NC}"; exit 1; }
}

log_info() { echo -e "${CYAN}[*] $1${NC}"; }
log_warn() { echo -e "${YELLOW}[!] $1${NC}"; }
log_succ() { echo -e "${GREEN}[+] $1${NC}"; }
log_err()  { echo -e "${RED}[x] $1${NC}"; }

# --- Global State ---
DRY_RUN=0
INTERACTIVE=1
RESTORE=0
DISABLE_MITIGATIONS=0
BACKUP_DIR="/etc/tuneperf/backup"
PROFILE_FILE="/etc/tuneperf/profile.conf"
SYSCTL_DIR="/etc/sysctl.d"
LOG_FILE="/var/log/tuneperf.log"

log_to_file() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1" >> "$LOG_FILE"
}

parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --dry-run) DRY_RUN=1; INTERACTIVE=0 ;;
            --apply) INTERACTIVE=0 ;;
            --restore) RESTORE=1; INTERACTIVE=0 ;;
            --disable-mitigations) DISABLE_MITIGATIONS=1 ;;
            -h|--help)
                echo "Usage: tuneperf.sh [OPTIONS]"
                echo "  --apply                Run non-interactively (uses last saved profile or defaults)"
                echo "  --dry-run              Show what would be generated, do not apply"
                echo "  --restore              Restore sysctl backups and disable persistence"
                echo "  --disable-mitigations  DANGEROUS: Add mitigations=off to GRUB for max performance"
                exit 0
                ;;
            *) log_err "Unknown option: $1"; exit 1 ;;
        esac
        shift
    done
}

detect_hardware() {
    log_info "Detecting advanced hardware topology..."

    kernel_ver=$(uname -r)
    echo " - Kernel: $kernel_ver"

    # RAM & HugePages
    ram_kb=$(awk '/MemTotal/ {print $2}' /proc/meminfo)
    ram_gb=$((ram_kb / 1024 / 1024))
    hugepages_size=$(awk '/Hugepagesize/ {print $2" "$3}' /proc/meminfo || echo "Unknown")
    
    if [ "$ram_gb" -lt 4 ]; then ram_class="low";
    elif [ "$ram_gb" -lt 16 ]; then ram_class="medium";
    elif [ "$ram_gb" -lt 64 ]; then ram_class="high";
    else ram_class="extreme"; fi
    echo " - RAM: ${ram_gb} GB ($ram_class) | Hugepages: $hugepages_size"

    # CPU & NUMA
    cpu_cores=$(nproc 2>/dev/null || echo 2)
    numa_nodes=1
    if command -v lscpu >/dev/null 2>&1; then
        cpu_model=$(lscpu | awk -F: '/Model name/ {print $2}' | xargs || echo "Unknown CPU")
        cpu_max_mhz=$(lscpu | awk -F: '/CPU max MHz/ {print int($2); exit}' || echo "")
        [ -z "$cpu_max_mhz" ] && cpu_max_mhz=$(lscpu | awk -F: '/CPU MHz/ {print int($2); exit}' || echo "")
        numa_nodes=$(lscpu | awk -F: '/NUMA node\(s\)/ {print int($2)}' || echo 1)
    else
        cpu_model=$(grep -m1 "model name" /proc/cpuinfo | awk -F: '{print $2}' | xargs || echo "Unknown CPU")
        cpu_max_mhz=$(grep -m1 "cpu MHz" /proc/cpuinfo | awk -F: '{print int($2)}' || echo "")
    fi
    [ -z "$cpu_max_mhz" ] && cpu_max_mhz=2000
    if [ -z "$numa_nodes" ] || [ "$numa_nodes" -eq 0 ]; then
        numa_nodes=$(ls -d /sys/devices/system/node/node* 2>/dev/null | wc -l || echo 0)
        [ "$numa_nodes" -eq 0 ] && numa_nodes=1
    fi

    echo " - CPU: $cpu_cores cores @ ~${cpu_max_mhz}MHz - $cpu_model (NUMA nodes: $numa_nodes)"

    # Mitigations
    mitigations_active=0
    if [ -d /sys/devices/system/cpu/vulnerabilities ]; then
        if grep -q "Vulnerable" /sys/devices/system/cpu/vulnerabilities/* 2>/dev/null; then
            mitigations_active=1
        elif ! grep -q "Not affected" /sys/devices/system/cpu/vulnerabilities/meltdown 2>/dev/null; then
            mitigations_active=1
        fi
    fi

    # GPU
    gpu_vendor="Unknown"
    gpu_info=""
    if command -v lspci >/dev/null 2>&1; then
        gpu_info=$(lspci 2>/dev/null | grep -iE "VGA|3D|Display" | awk -F": " '{print $2}' | head -n1 || echo "")
        if echo "$gpu_info" | grep -qi "nvidia"; then gpu_vendor="Nvidia";
        elif echo "$gpu_info" | grep -qi "amd\|radeon"; then gpu_vendor="AMD";
        elif echo "$gpu_info" | grep -qi "intel"; then gpu_vendor="Intel"; fi
    fi
    echo " - GPU: ${gpu_info:-None} ($gpu_vendor)"

    # Storage (Root)
    disk_type="hdd"
    root_part=$(df / 2>/dev/null | awk 'NR==2 {print $1}' || echo "")
    base_dev=""
    
    if [ -n "$root_part" ] && [[ "$root_part" == /dev/* ]]; then
        real_part=$(readlink -f "$root_part" || echo "$root_part")
        if command -v lsblk >/dev/null 2>&1; then
            base_dev=$(lsblk -no pkname "$real_part" 2>/dev/null | head -n1 || echo "")
        fi
        [ -z "$base_dev" ] && base_dev=$(basename "$real_part" | sed -r 's/p?[0-9]+$//')

        if [[ "$base_dev" == nvme* ]]; then disk_type="nvme"
        elif [ -f "/sys/block/$base_dev/queue/rotational" ]; then
            if [ "$(cat "/sys/block/$base_dev/queue/rotational" 2>/dev/null || echo 1)" -eq 0 ]; then disk_type="ssd"
            else disk_type="hdd"; fi
        fi
    fi
    echo " - Storage (Root): $disk_type (Device: ${base_dev:-unknown})"
    
    root_fs=$(findmnt -n -o FSTYPE / 2>/dev/null || echo "unknown")
    echo " - Filesystem (Root): $root_fs"

    # Swap & ZRAM
    has_zram=0; has_swap=0
    if command -v swapon >/dev/null 2>&1; then
        if swapon --show 2>/dev/null | grep -q "zram"; then has_zram=1; fi
        if swapon --show 2>/dev/null | grep -v "zram\|NAME" | grep -q "/"; then has_swap=1; fi
    fi
    echo " - Memory Swap: ZRAM ($has_zram) | Classic Swap ($has_swap)"

    # Form factor
    is_laptop=0; on_battery=0; is_vm=0; hypervisor="none"
    if command -v systemd-detect-virt >/dev/null 2>&1; then
        hypervisor=$(systemd-detect-virt || echo "none")
        if [ "$hypervisor" != "none" ]; then is_vm=1; fi
    fi

    if [ -f /sys/class/dmi/id/chassis_type ]; then
        ctype=$(cat /sys/class/dmi/id/chassis_type 2>/dev/null || echo "")
        if [[ "$ctype" =~ ^(8|9|10|11|14|31|32)$ ]] || ls -1 /sys/class/power_supply/ 2>/dev/null | grep -q "^BAT"; then is_laptop=1; fi
    elif ls -1 /sys/class/power_supply/ 2>/dev/null | grep -q "^BAT"; then is_laptop=1; fi

    if [ "$is_laptop" -eq 1 ]; then
        if grep -q "0" /sys/class/power_supply/*/online 2>/dev/null || grep -q "Discharging" /sys/class/power_supply/*/status 2>/dev/null; then
            on_battery=1
            echo " - Form Factor: Laptop (Currently ON BATTERY)"
        else
            echo " - Form Factor: Laptop (Currently ON AC)"
        fi
    elif [ "$is_vm" -eq 1 ]; then
        echo " - Form Factor: Virtual Machine ($hypervisor)"
    else
        echo " - Form Factor: Desktop/Server"
    fi
}

ask_profile() {
    log_info "Please define the system role and usage:"
    echo "  1) Laptop / Desktop (General Usage)"
    echo "  2) Workstation (Development, Creation)"
    echo "  3) Gaming Machine"
    echo "  4) Server (Light/Medium Load)"
    echo "  5) Server (High Load / Database)"
    echo "  6) Virtual Machine (Guest)"
    read -rp "Select Role [1-6] (Default 1): " choice_role
    if [[ ! "$choice_role" =~ ^[1-6]$ ]]; then choice_role=1; fi

    echo ""
    case "$choice_role" in
        1)
            echo "  1) Web Browsing & Office (Light)"
            echo "  2) Heavy Browsing & Media (Many tabs, 4k video)"
            echo "  3) Home Theater PC (HTPC / Media Center)"
            echo "  4) Light coding / Scripts"
            read -rp "Select Usage [1-4] (Default 1): " choice_usage ;;
        2)
            echo "  1) Code Compilation (Heavy CPU/RAM)"
            echo "  2) Docker / Containers Host"
            echo "  3) Virtual Machines Host"
            echo "  4) Audio / Video Processing (Low latency, high IO)"
            echo "  5) AI / Machine Learning (Heavy GPU/Memory)"
            read -rp "Select Usage [1-5] (Default 1): " choice_usage ;;
        3)
            echo "  1) AAA Games / Intensive 3D"
            echo "  2) Competitive E-sports (Low Latency / High FPS focus)"
            echo "  3) Emulation (Heavy single-thread CPU)"
            echo "  4) Cloud Gaming / Streaming"
            read -rp "Select Usage [1-4] (Default 1): " choice_usage ;;
        4)
            echo "  1) Web Server / Reverse Proxy (Nginx, Apache)"
            echo "  2) File Server (NAS, Samba, NFS)"
            echo "  3) Media Server (Plex, Jellyfin)"
            echo "  4) General Purpose / Mixed (Nextcloud, minor DBs)"
            read -rp "Select Usage [1-4] (Default 1): " choice_usage ;;
        5)
            echo "  1) Heavy Database (PostgreSQL, MySQL/MariaDB)"
            echo "  2) In-Memory DB / Cache (Redis, Memcached)"
            echo "  3) High-Traffic Web/App Server"
            echo "  4) Big Data / Analytics (Elasticsearch, Hadoop)"
            read -rp "Select Usage [1-4] (Default 1): " choice_usage ;;
        6)
            echo "  1) General Purpose / Testing"
            echo "  2) CI/CD Runner / Build Node"
            echo "  3) Sandboxed Application (Docker inside VM)"
            echo "  4) Desktop / GUI usage in VM"
            read -rp "Select Usage [1-4] (Default 1): " choice_usage ;;
        *)
            echo "  1) General Usage"
            read -rp "Select Usage [1] (Default 1): " choice_usage ;;
    esac
    if [[ ! "$choice_usage" =~ ^[1-6]$ ]]; then choice_usage=1; fi

    echo ""
    read -rp "IPv6 Policy [1=Keep, 2=Disable Completely, 3=Local-Only] (Default 1): " choice_ipv6
    if [[ ! "$choice_ipv6" =~ ^[1-3]$ ]]; then choice_ipv6=1; fi

    echo ""
    log_info "EXPERIMENTAL MODE"
    echo "This unlocks aggressive network/disk buffers, KSM, Hugepages, Systemd limits and strict latency parameters."
    echo "It may reduce observability (disabling IO stats, NMI watchdog) and increase bufferbloat if not careful."
    read -rp "Enable Experimental tweaks? [y/N]: " choice_exp
    if [[ "$choice_exp" =~ ^[Yy]$ ]]; then exp_enabled=1; else exp_enabled=0; fi

    mkdir -p /etc/tuneperf
    cat > "$PROFILE_FILE" <<EOF
choice_role="$choice_role"
choice_usage="$choice_usage"
choice_ipv6="$choice_ipv6"
exp_enabled="$exp_enabled"
EOF
}

load_profile() {
    if [ -f "$PROFILE_FILE" ]; then
        choice_role=$(grep "^choice_role=" "$PROFILE_FILE" | cut -d= -f2 | tr -d '"' || echo 1)
        choice_usage=$(grep "^choice_usage=" "$PROFILE_FILE" | cut -d= -f2 | tr -d '"' || echo 1)
        choice_ipv6=$(grep "^choice_ipv6=" "$PROFILE_FILE" | cut -d= -f2 | tr -d '"' || echo 1)
        exp_enabled=$(grep "^exp_enabled=" "$PROFILE_FILE" | cut -d= -f2 | tr -d '"' || echo 0)
        log_info "Loaded profile from $PROFILE_FILE"
    else
        log_warn "No profile found, using defaults (Desktop/General)"
        choice_role=1; choice_usage=1; choice_ipv6=1; exp_enabled=0
    fi
}

calculate_parameters() {
    # --- SYSCTL VARS ---
    sys_swappiness=60
    sys_vfs_cache_pressure=100
    sys_page_cluster=3
    sys_dirty_ratio=""; sys_dirty_bg_ratio=""
    sys_dirty_bytes=""; sys_dirty_bg_bytes=""
    sys_dirty_expire=3000; sys_dirty_writeback=500
    sys_dirtytime_expire=43200
    
    sys_watermark_scale_factor=50
    sys_watermark_boost_factor=15000
    sys_compaction=""
    sys_zone_reclaim=0
    sys_extfrag_threshold=500
    sys_stat_interval=""
    sys_mmap_min_addr=65536
    
    sys_max_map_count=65530
    sys_min_free_kbytes=65536
    sys_overcommit_memory=""
    sys_overcommit_ratio=""
    sys_nr_hugepages=""
    sys_pid_max=65536
    sys_file_max=2097152
    sys_aio_max_nr=65536
    sys_inotify_watches=524288

    # IPC (SysV)
    sys_shmmax=$((ram_kb * 1024 / 2))
    sys_shmall=$((sys_shmmax / 4096))
    sys_msgmax=65536
    sys_msgmnb=65536

    # Network
    sys_tcp_fastopen=3; sys_tcp_fin_timeout=15
    sys_somaxconn=4096; sys_rmem_max=16777216; sys_wmem_max=16777216
    sys_rmem_def=262144; sys_wmem_def=262144
    sys_tcp_rmem="4096 87380 16777216"; sys_tcp_wmem="4096 65536 16777216"
    sys_tcp_notsent_lowat=""
    sys_tcp_tw_reuse=""
    sys_tcp_slow_start=0
    sys_tcp_retries2=5; sys_tcp_syn_retries=2
    sys_tcp_synack_retries=2
    sys_tcp_early_retrans=1
    sys_tcp_rfc1337=1
    sys_tcp_limit_output_bytes=""
    sys_tcp_mtu_probing=1
    sys_tcp_syncookies=1
    sys_tcp_window_scaling=1
    sys_tcp_max_syn_backlog=4096
    sys_netdev_max_backlog=2500
    sys_netdev_budget=300
    sys_netdev_budget_usecs=8000
    sys_tcp_max_tw_buckets=65536
    sys_tcp_moderate_rcvbuf=1
    sys_tcp_abort_on_overflow=0
    
    sys_core_uses_pid=1
    sys_bbr=0; sys_tcp_cc="cubic"
    sys_ip_local_port_range=""; sys_tcp_keepalive_time=""; sys_tcp_keepalive_intvl=""; sys_tcp_keepalive_probes=""
    sys_tcp_autocorking=""
    sys_txqueuelen=1000
    sys_nf_conntrack_max=$((ram_kb * 1024 / 16384))
    
    # Kernel & Sched
    sys_migration_cost=5000000; sys_timer_migration=1; sys_autogroup=1
    sys_sched_latency=""; sys_sched_min_granularity=""
    sys_sched_wakeup_granularity=""
    sys_sched_nr_migrate=32
    sys_child_runs_first=""
    sys_cfs_bandwidth_slice=""
    sys_numa_balancing=""
    sys_nmi_watchdog=1
    sys_perf_event_paranoid=""
    
    # Security
    sys_randomize_va_space=2
    sys_kptr_restrict=1
    sys_dmesg_restrict=1
    sys_unprivileged_bpf_disabled=1
    sys_bpf_jit_harden=2
    sys_ptrace_scope=1
    sys_protected_hardlinks=1
    sys_protected_symlinks=1
    sys_protected_fifos=2
    sys_protected_regular=2

    # --- SYSFS VARS ---
    sysfs_thp="madvise"
    sysfs_thp_defrag="madvise"
    sysfs_governor="schedutil"
    sysfs_energy_perf="balance_performance"
    sysfs_gpu_amd="auto"
    sysfs_ksm=0
    sysfs_ksm_pages_to_scan=100
    sysfs_ksm_sleep_millisecs=20
    sysfs_ksm_merge_across_nodes=0
    sysfs_iostats=""
    sysfs_eevdf_slice=""

    # Logic: Memory Scaling (0.5% of RAM, capped at 512MB, minimum 64MB)
    sys_min_free_kbytes=$((ram_kb * 5 / 1000))
    if [ "$sys_min_free_kbytes" -lt 65536 ]; then sys_min_free_kbytes=65536; fi
    if [ "$sys_min_free_kbytes" -gt 524288 ]; then sys_min_free_kbytes=524288; fi

    if [ "$has_zram" -eq 1 ]; then
        sys_swappiness=100
        sys_page_cluster=0
    else
        if [ "$disk_type" == "ssd" ] || [ "$disk_type" == "nvme" ]; then
            sys_page_cluster=1
        fi
        
        if [ "$ram_class" == "extreme" ]; then sys_swappiness=10;
        elif [ "$ram_class" == "high" ]; then sys_swappiness=20;
        elif [ "$ram_class" == "medium" ]; then sys_swappiness=40;
        fi
        [ "$disk_type" == "hdd" ] && sys_swappiness=$((sys_swappiness - 10))
        [ "$sys_swappiness" -lt 5 ] && sys_swappiness=5
        [ "$disk_type" == "nvme" ] && sys_swappiness=$((sys_swappiness + 10))
    fi

    is_server=0
    if [[ "$choice_role" == "4" || "$choice_role" == "5" ]]; then is_server=1; fi

    if [ "$is_server" -eq 1 ]; then
        sysfs_governor="performance"
        sys_vfs_cache_pressure=100
        sys_tcp_fin_timeout=30
        sys_txqueuelen=3000
        sys_page_cluster=0
        
        # Scale backlogs with RAM
        if [ "$ram_gb" -lt 4 ]; then
            sys_somaxconn=8192; sys_tcp_max_syn_backlog=8192; sys_netdev_max_backlog=8192
        elif [ "$ram_gb" -lt 16 ]; then
            sys_somaxconn=32768; sys_tcp_max_syn_backlog=32768; sys_netdev_max_backlog=32768
        else
            sys_somaxconn=65535; sys_tcp_max_syn_backlog=65536; sys_netdev_max_backlog=65536
        fi
        
        sys_rmem_max=67108864; sys_wmem_max=67108864
        sys_rmem_def=262144; sys_wmem_def=262144
        sys_tcp_rmem="4096 87380 67108864"; sys_tcp_wmem="4096 65536 67108864"
        sys_ip_local_port_range="1024 65535"
        sys_tcp_keepalive_time=600; sys_tcp_keepalive_intvl=30; sys_tcp_keepalive_probes=5
        sys_pid_max=4194304
        
        if [ "$is_eevdf" -eq 1 ]; then
            sys_sched_latency=""; sys_sched_min_granularity=""; sys_sched_wakeup_granularity=""
            sysfs_eevdf_slice=8000000
        else
            sys_sched_latency=24000000; sys_sched_min_granularity=3000000; sys_sched_wakeup_granularity=3000000
            sysfs_eevdf_slice=""
        fi
        
        sys_watermark_scale_factor=200
        sys_watermark_boost_factor=""
        sys_dirtytime_expire=3600
        sys_extfrag_threshold=750
        sys_file_max=4194304
        sys_inotify_watches=2097152
        sys_netdev_budget=600
        sys_tcp_max_tw_buckets=2000000
    else
        sys_vfs_cache_pressure=50
        [ "$ram_gb" -ge 16 ] && sys_vfs_cache_pressure=30
        sys_watermark_scale_factor=50
        
        if [ "$is_eevdf" -eq 1 ]; then
            sys_sched_latency=""; sys_sched_min_granularity=""; sys_sched_wakeup_granularity=""
            sysfs_eevdf_slice=1000000
        else
            sys_sched_latency=6000000; sys_sched_min_granularity=750000; sys_sched_wakeup_granularity=1500000
            sysfs_eevdf_slice=""
        fi
        
        sys_child_runs_first=1
        sys_inotify_watches=2097152
    fi

    # Containers / VMs
    if [[ "$choice_role" == "2" && "$choice_usage" =~ ^(2|3)$ ]] || [ "$choice_role" == "6" ]; then
        sys_overcommit_memory=2
        sys_numa_balancing=0
    fi
    if [ "$is_vm" -eq 1 ] && [ "$numa_nodes" -eq 1 ]; then
        sys_numa_balancing=0
    fi

    # Dirty Settings
    if [ "$ram_gb" -ge 32 ]; then
        if [[ "$disk_type" == "nvme" ]]; then
            sys_dirty_bytes=268435456
            sys_dirty_bg_bytes=134217728
        else
            sys_dirty_bytes=536870912
            sys_dirty_bg_bytes=134217728
        fi
    elif [ "$ram_gb" -ge 8 ]; then
        if [ "$disk_type" == "nvme" ]; then sys_dirty_bytes=268435456; sys_dirty_bg_bytes=134217728;
        elif [ "$disk_type" == "ssd" ]; then sys_dirty_bytes=335544320; sys_dirty_bg_bytes=167772160;
        else sys_dirty_bytes=134217728; sys_dirty_bg_bytes=67108864; fi
    else
        if [[ "$disk_type" == "nvme" || "$disk_type" == "ssd" ]]; then sys_dirty_ratio=15; sys_dirty_bg_ratio=5;
        else sys_dirty_ratio=20; sys_dirty_bg_ratio=10; fi
    fi

    # Overrides based on Role/Usage
    if [[ "$choice_role" == "5" && "$choice_usage" == "1" ]]; then
        # DB Server
        sys_swappiness=10
        sys_dirty_bytes=""; sys_dirty_ratio=10; sys_dirty_bg_ratio=3
        sys_compaction=0
        sysfs_thp="never"
        sysfs_thp_defrag="never"
        sys_nr_hugepages=$((ram_kb / 20 / 2048))
        if [ "$sys_nr_hugepages" -gt 2048 ]; then sys_nr_hugepages=2048; fi
        if [ "$is_eevdf" -eq 1 ]; then sysfs_eevdf_slice=8000000; fi
        sysfs_governor="performance"
        sysfs_energy_perf="performance"
        sys_aio_max_nr=1048576
    elif [[ "$choice_role" == "3" ]]; then
        # Gaming
        sysfs_governor="performance"
        sysfs_energy_perf="performance"
        sysfs_gpu_amd="high"
        sys_tcp_autocorking=0
        sys_max_map_count=1048576
    elif [[ "$choice_role" == "2" && "$choice_usage" == "4" ]]; then
        # Audio/Video
        sysfs_governor="performance"
        sysfs_energy_perf="performance"
    elif [[ "$choice_role" == "4" && "$choice_usage" == "2" ]]; then
        # File Server
        sys_dirty_expire=10000
        sys_dirty_writeback=1500
        sys_vfs_cache_pressure=20
    fi

    # Battery Override
    if [ "$on_battery" -eq 1 ]; then
        sys_nmi_watchdog=0
        sys_timer_migration=1
        sys_dirty_writeback=1500; sys_dirty_expire=5000
        sysfs_governor="powersave"
        sysfs_energy_perf="power"
        sysfs_gpu_amd="low"
        sys_txqueuelen=500
    fi

    # --- EXPERIMENTAL OVERRIDES ---
    if [ "$exp_enabled" -eq 1 ]; then
        log_warn "Experimental mode: Applying aggressive heuristics (hugepages, KSM, iostats=0, zero-migration)."
        
        [ "$is_server" -eq 1 ] && sys_tcp_tw_reuse=1
        [ "$is_server" -eq 1 ] && sys_tcp_notsent_lowat=16384
        [ "$is_server" -eq 1 ] && sys_stat_interval=10
        [ "$numa_nodes" -gt 1 ] && sys_zone_reclaim=1
        
        sysfs_iostats="0"

        if [[ "$choice_role" == "3" ]] || [[ "$choice_role" == "2" && "$choice_usage" == "4" ]]; then
            sys_timer_migration=0
            sys_migration_cost=500000
            sys_sched_nr_migrate=8
            sys_nmi_watchdog=0
            sys_perf_event_paranoid=1
        elif [[ "$choice_role" == "5" && "$choice_usage" == "1" ]]; then
            # Pre-allocate 5% of RAM in HugePages for DB
            sys_nr_hugepages=$((ram_kb / 20 / 2048)) 
        fi
        
        if [[ "$choice_role" == "2" && "$choice_usage" =~ ^(2|3)$ ]] || [ "$choice_role" == "6" ]; then
            sysfs_ksm=1
            sysfs_ksm_pages_to_scan=2000
            sysfs_ksm_sleep_millisecs=200
            [ "$numa_nodes" -gt 1 ] && sysfs_ksm_merge_across_nodes=1
        fi

        if modprobe tcp_bbr 2>/dev/null || grep -q "bbr" /proc/sys/net/ipv4/tcp_available_congestion_control 2>/dev/null; then
            sys_bbr=1
            sys_tcp_cc="bbr"
        fi
    fi
}

validate_and_save_sysctl() {
    local file=$1
    local temp="${file}.tmp"
    > "$temp"
    while IFS= read -r line; do
        if echo "$line" | grep -q "^[a-z]"; then
            key=$(echo "$line" | awk -F= '{print $1}' | tr -d ' ')
            key_path=$(echo "$key" | tr '.' '/')
            if [ -e "/proc/sys/$key_path" ]; then
                echo "$line" >> "$temp"
            else
                log_warn "Kernel doesn't support $key, ignoring."
            fi
        else
            echo "$line" >> "$temp"
        fi
    done < "$file"
    mv "$temp" "$file"
}

generate_sysctl() {
    log_info "Generating sysctl configs..."
    
    kernel_major=$(uname -r | cut -d. -f1 2>/dev/null || echo 5)
    kernel_minor=$(uname -r | cut -d. -f2 2>/dev/null || echo 0)
    is_eevdf=0
    if [ "$kernel_major" -gt 6 ] || { [ "$kernel_major" -eq 6 ] && [ "$kernel_minor" -ge 6 ]; }; then
        is_eevdf=1
    fi
    mkdir -p /etc/tuneperf/generated
    OUT_SWAP="/etc/tuneperf/generated/99-tuneperf-swappiness.conf"
    OUT_PERF="/etc/tuneperf/generated/99-tuneperf-perfs.conf"
    OUT_NET="/etc/tuneperf/generated/99-tuneperf-network.conf"
    OUT_SEC="/etc/tuneperf/generated/99-tuneperf-security.conf"
    OUT_MODULES="/etc/tuneperf/generated/tuneperf-modules.conf"

    cat > "$OUT_SWAP" <<EOF
# Generated by TunePerf
vm.swappiness=$sys_swappiness
vm.vfs_cache_pressure=$sys_vfs_cache_pressure
vm.page-cluster=$sys_page_cluster
vm.min_free_kbytes=$sys_min_free_kbytes
vm.max_map_count=$sys_max_map_count
vm.dirtytime_expire_seconds=$sys_dirtytime_expire
vm.watermark_scale_factor=$sys_watermark_scale_factor
vm.watermark_boost_factor=$sys_watermark_boost_factor
vm.extfrag_threshold=$sys_extfrag_threshold
EOF
    [ -n "$sys_stat_interval" ] && echo "vm.stat_interval=$sys_stat_interval" >> "$OUT_SWAP"
    [ -n "$sys_zone_reclaim" ] && echo "vm.zone_reclaim_mode=$sys_zone_reclaim" >> "$OUT_SWAP"
    [ -n "$sys_compaction" ] && echo "vm.compaction_proactiveness=$sys_compaction" >> "$OUT_SWAP"
    [ -n "$sys_overcommit_memory" ] && echo "vm.overcommit_memory=$sys_overcommit_memory" >> "$OUT_SWAP"
    [ -n "$sys_overcommit_ratio" ] && echo "vm.overcommit_ratio=$sys_overcommit_ratio" >> "$OUT_SWAP"
    [ -n "$sys_nr_hugepages" ] && echo "vm.nr_hugepages=$sys_nr_hugepages" >> "$OUT_SWAP"

    cat > "$OUT_PERF" <<EOF
# Generated by TunePerf: Disk & Sched & System
fs.file-max = $sys_file_max
fs.aio-max-nr = $sys_aio_max_nr
fs.inotify.max_user_watches = $sys_inotify_watches

kernel.sched_migration_cost_ns = $sys_migration_cost
kernel.timer_migration=$sys_timer_migration
kernel.sched_autogroup_enabled=$sys_autogroup
kernel.nmi_watchdog=$sys_nmi_watchdog
kernel.pid_max=$sys_pid_max
kernel.core_uses_pid=$sys_core_uses_pid
kernel.sched_nr_migrate=$sys_sched_nr_migrate
kernel.shmmax=$sys_shmmax
kernel.shmall=$sys_shmall
kernel.msgmax=$sys_msgmax
kernel.msgmnb=$sys_msgmnb
EOF
    if [ -n "$sys_dirty_bytes" ]; then
        echo -e "vm.dirty_bytes = $sys_dirty_bytes\nvm.dirty_background_bytes = $sys_dirty_bg_bytes" >> "$OUT_PERF"
    elif [ -n "$sys_dirty_ratio" ]; then
        echo -e "vm.dirty_ratio = $sys_dirty_ratio\nvm.dirty_background_ratio = $sys_dirty_bg_ratio" >> "$OUT_PERF"
    fi
    
    echo "vm.dirty_writeback_centisecs = $sys_dirty_writeback" >> "$OUT_PERF"
    echo "vm.dirty_expire_centisecs = $sys_dirty_expire" >> "$OUT_PERF"

    [ -n "$sys_sched_latency" ] && echo "kernel.sched_latency_ns=$sys_sched_latency" >> "$OUT_PERF"
    [ -n "$sys_sched_min_granularity" ] && echo "kernel.sched_min_granularity_ns=$sys_sched_min_granularity" >> "$OUT_PERF"
    [ -n "$sys_sched_wakeup_granularity" ] && echo "kernel.sched_wakeup_granularity_ns=$sys_sched_wakeup_granularity" >> "$OUT_PERF"
    [ -n "$sys_child_runs_first" ] && echo "kernel.sched_child_runs_first=$sys_child_runs_first" >> "$OUT_PERF"
    [ -n "$sys_cfs_bandwidth_slice" ] && echo "kernel.sched_cfs_bandwidth_slice_us=$sys_cfs_bandwidth_slice" >> "$OUT_PERF"
    [ -n "$sys_numa_balancing" ] && echo "kernel.numa_balancing=$sys_numa_balancing" >> "$OUT_PERF"

    cat > "$OUT_NET" <<EOF
# Generated by TunePerf: Network
net.ipv4.tcp_fastopen=$sys_tcp_fastopen
net.ipv4.tcp_timestamps=1
net.ipv4.tcp_sack=1
net.ipv4.tcp_fin_timeout=$sys_tcp_fin_timeout
net.ipv4.tcp_slow_start_after_idle=$sys_tcp_slow_start
net.ipv4.tcp_congestion_control=$sys_tcp_cc
net.ipv4.tcp_retries2=$sys_tcp_retries2
net.ipv4.tcp_syn_retries=$sys_tcp_syn_retries
net.ipv4.tcp_synack_retries=$sys_tcp_synack_retries
net.ipv4.tcp_early_retrans=$sys_tcp_early_retrans
net.ipv4.tcp_mtu_probing=$sys_tcp_mtu_probing
net.ipv4.tcp_syncookies=$sys_tcp_syncookies
net.ipv4.tcp_window_scaling=$sys_tcp_window_scaling
net.ipv4.tcp_moderate_rcvbuf=$sys_tcp_moderate_rcvbuf
net.ipv4.tcp_abort_on_overflow=$sys_tcp_abort_on_overflow
net.ipv4.tcp_max_tw_buckets=$sys_tcp_max_tw_buckets

net.core.somaxconn=$sys_somaxconn
net.core.rmem_max=$sys_rmem_max
net.core.wmem_max=$sys_wmem_max
net.core.rmem_default=$sys_rmem_def
net.core.wmem_default=$sys_wmem_def
net.ipv4.tcp_rmem=$sys_tcp_rmem
net.ipv4.tcp_wmem=$sys_tcp_wmem
net.ipv4.tcp_max_syn_backlog=$sys_tcp_max_syn_backlog
net.core.netdev_max_backlog=$sys_netdev_max_backlog
net.core.netdev_budget=$sys_netdev_budget
net.core.netdev_budget_usecs=$sys_netdev_budget_usecs
EOF
    if lsmod 2>/dev/null | grep -q "^nf_conntrack"; then
        echo "net.netfilter.nf_conntrack_max=$sys_nf_conntrack_max" >> "$OUT_NET"
        echo "net.netfilter.nf_conntrack_tcp_timeout_established=43200" >> "$OUT_NET"
    fi
    [ -n "$sys_tcp_limit_output_bytes" ] && echo "net.ipv4.tcp_limit_output_bytes=$sys_tcp_limit_output_bytes" >> "$OUT_NET"
    [ -n "$sys_tcp_tw_reuse" ] && echo "net.ipv4.tcp_tw_reuse=$sys_tcp_tw_reuse" >> "$OUT_NET"
    [ -n "$sys_tcp_notsent_lowat" ] && echo "net.ipv4.tcp_notsent_lowat=$sys_tcp_notsent_lowat" >> "$OUT_NET"
    [ "$sys_bbr" -eq 1 ] && echo "net.core.default_qdisc=fq" >> "$OUT_NET"
    [ -n "$sys_ip_local_port_range" ] && echo "net.ipv4.ip_local_port_range = $sys_ip_local_port_range" >> "$OUT_NET"
    [ -n "$sys_tcp_keepalive_time" ] && echo "net.ipv4.tcp_keepalive_time = $sys_tcp_keepalive_time" >> "$OUT_NET"
    [ -n "$sys_tcp_keepalive_intvl" ] && echo "net.ipv4.tcp_keepalive_intvl = $sys_tcp_keepalive_intvl" >> "$OUT_NET"
    [ -n "$sys_tcp_keepalive_probes" ] && echo "net.ipv4.tcp_keepalive_probes = $sys_tcp_keepalive_probes" >> "$OUT_NET"
    [ -n "$sys_tcp_autocorking" ] && echo "net.ipv4.tcp_autocorking=$sys_tcp_autocorking" >> "$OUT_NET"

    if [ "$choice_ipv6" -eq 2 ]; then
        echo -e "\n# Disable IPv6\nnet.ipv6.conf.all.disable_ipv6 = 1\nnet.ipv6.conf.default.disable_ipv6 = 1\nnet.ipv6.conf.lo.disable_ipv6 = 1" >> "$OUT_NET"
    elif [ "$choice_ipv6" -eq 3 ]; then
        echo -e "\n# IPv6 Local Only\nnet.ipv6.conf.all.accept_ra = 0\nnet.ipv6.conf.default.accept_ra = 0\nnet.ipv6.conf.all.autoconf = 0\nnet.ipv6.conf.default.autoconf = 0" >> "$OUT_NET"
    fi

    cat > "$OUT_SEC" <<EOF
# Generated by TunePerf: Hardening
vm.mmap_min_addr=$sys_mmap_min_addr
kernel.randomize_va_space=$sys_randomize_va_space
kernel.kptr_restrict=$sys_kptr_restrict
kernel.dmesg_restrict=$sys_dmesg_restrict
kernel.unprivileged_bpf_disabled=$sys_unprivileged_bpf_disabled
net.core.bpf_jit_enable=1
net.core.bpf_jit_harden=$sys_bpf_jit_harden
kernel.yama.ptrace_scope=$sys_ptrace_scope
kernel.kexec_load_disabled=1
fs.protected_hardlinks=$sys_protected_hardlinks
fs.protected_symlinks=$sys_protected_symlinks
fs.protected_fifos=$sys_protected_fifos
fs.protected_regular=$sys_protected_regular
fs.suid_dumpable=0
net.ipv4.tcp_rfc1337=$sys_tcp_rfc1337
EOF
    [ -n "$sys_perf_event_paranoid" ] && echo "kernel.perf_event_paranoid=$sys_perf_event_paranoid" >> "$OUT_SEC"
    cat >> "$OUT_SEC" <<EOF
net.ipv4.conf.all.accept_redirects=0
net.ipv4.conf.default.accept_redirects=0
net.ipv4.conf.all.send_redirects=0
net.ipv4.conf.all.log_martians=1
net.ipv4.conf.all.rp_filter=1
net.ipv4.icmp_echo_ignore_broadcasts=1
net.ipv4.icmp_ignore_bogus_error_responses=1
net.ipv4.conf.all.accept_source_route=0
net.ipv4.conf.all.arp_ignore=1
net.ipv4.conf.all.arp_announce=2
EOF

    > "$OUT_MODULES"
    if [ "$sys_bbr" -eq 1 ]; then
        echo "tcp_bbr" >> "$OUT_MODULES"
        echo "sch_fq" >> "$OUT_MODULES"
    fi

    validate_and_save_sysctl "$OUT_SWAP"
    validate_and_save_sysctl "$OUT_PERF"
    validate_and_save_sysctl "$OUT_NET"
    validate_and_save_sysctl "$OUT_SEC"
}

generate_systemd_persistence() {
    log_info "Generating systemd persistence and udev rules..."
    mkdir -p /etc/tuneperf/scripts
    OUT_SYSFS="/etc/tuneperf/scripts/apply_sysfs.sh"
    cat > "$OUT_SYSFS" <<EOF
#!/usr/bin/env bash
# Generated by TunePerf
set -u

# Variables
mode="\${1:-ac}"
if [ "\$mode" == "battery" ]; then
    dyn_gov="powersave"
    dyn_epp="power"
else
    dyn_gov="$sysfs_governor"
    dyn_epp="$sysfs_energy_perf"
fi

safe_write() {
    local file="\$1"
    local val="\$2"
    if [ -f "\$file" ]; then echo "\$val" > "\$file" 2>/dev/null || true; fi
}

# Helpers
set_io_scheduler() {
    local dev="\$1"
    local wanted="\$2"
    local sysfs="/sys/block/\$dev/queue/scheduler"
    [[ -f "\$sysfs" ]] || return 1
    local available=\$(cat "\$sysfs" 2>/dev/null || echo "")
    if [[ "\$available" =~ \[\$wanted\] ]]; then return 0; fi
    if echo "\$available" | grep -qw "\$wanted"; then
        echo "\$wanted" > "\$sysfs" 2>/dev/null || true
    fi
}

# 0. EEVDF Scheduler (Kernel 6.6+) via DebugFS
if [ -n "$sysfs_eevdf_slice" ]; then
    if ! mountpoint -q /sys/kernel/debug 2>/dev/null; then
        mount -t debugfs none /sys/kernel/debug 2>/dev/null || true
    fi
    if [ -d /sys/kernel/debug/sched ]; then
        safe_write /sys/kernel/debug/sched/base_slice_ns "$sysfs_eevdf_slice"
    fi
fi

# 1. THP & Khugepaged
safe_write /sys/kernel/mm/transparent_hugepage/enabled "$sysfs_thp"
safe_write /sys/kernel/mm/transparent_hugepage/defrag "$sysfs_thp_defrag"
if [ "$sysfs_thp" == "madvise" ] || [ "$sysfs_thp" == "always" ]; then
    safe_write /sys/kernel/mm/transparent_hugepage/khugepaged/scan_sleep_millisecs 5000
    safe_write /sys/kernel/mm/transparent_hugepage/khugepaged/alloc_sleep_millisecs 2000
    safe_write /sys/kernel/mm/transparent_hugepage/khugepaged/max_ptes_shared 256
fi

# 2. Block Devices Iteration (incl. ZRAM)
for block_dev in /sys/block/sd* /sys/block/nvme* /sys/block/vd*; do
    [ -e "\$block_dev" ] || continue
    dev_name=\$(basename "\$block_dev")
    is_rotational=\$(cat "\$block_dev/queue/rotational" 2>/dev/null || echo 1)
    
    if [[ "\$dev_name" == zram* ]]; then
        if grep -q "zstd" "\$block_dev/comp_algorithm" 2>/dev/null; then
            safe_write "\$block_dev/comp_algorithm" "zstd"
        else
            safe_write "\$block_dev/comp_algorithm" "lz4"
        fi
        safe_write "\$block_dev/max_comp_streams" "\$(nproc 2>/dev/null || echo 1)"
        continue
    fi
    
    if [[ "\$dev_name" == nvme* ]]; then
        set_io_scheduler "\$dev_name" "none"
        safe_write "\$block_dev/queue/nomerges" "1"
        safe_write "\$block_dev/queue/read_ahead_kb" "128"
        safe_write "\$block_dev/queue/nr_requests" "256"
        safe_write "\$block_dev/queue/add_random" "0"
        safe_write "\$block_dev/queue/max_sectors_kb" "4096"
        [ -n "$sysfs_iostats" ] && safe_write "\$block_dev/queue/iostats" "$sysfs_iostats"
    elif [ "\$is_rotational" -eq 0 ]; then
        set_io_scheduler "\$dev_name" "mq-deadline"
        safe_write "\$block_dev/queue/read_ahead_kb" "1024"
        safe_write "\$block_dev/queue/add_random" "0"
        safe_write "\$block_dev/queue/nr_requests" "128"
    else
        modprobe bfq 2>/dev/null || true
        set_io_scheduler "\$dev_name" "bfq"
        safe_write "\$block_dev/queue/read_ahead_kb" "2048"
        safe_write "\$block_dev/queue/nr_requests" "64"
    fi
done

# 3. CPU Governors & ASPM
drv=\$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_driver 2>/dev/null || echo "unknown")
for policy in /sys/devices/system/cpu/cpufreq/policy*; do
    [ -d "\$policy" ] || continue
    if [[ "\$drv" != "intel_pstate" && "\$drv" != "amd-pstate-epp" ]]; then
        safe_write "\$policy/scaling_governor" "\$dyn_gov"
    fi
    safe_write "\$policy/energy_performance_preference" "\$dyn_epp"
done

if [ -d "/sys/devices/system/cpu/amd-pstate" ]; then
    safe_write "/sys/devices/system/cpu/amd-pstate/status" "active"
fi

safe_write /sys/module/pcie_aspm/parameters/policy "performance"

# 4. GPU Power Management
if [ "$gpu_vendor" == "Nvidia" ] && command -v nvidia-smi >/dev/null 2>&1; then
    nvidia-smi -pm 1 >/dev/null 2>&1 || true
elif [ "$gpu_vendor" == "AMD" ]; then
    for card in /sys/class/drm/card*/device/power_dpm_force_performance_level; do
        safe_write "\$card" "$sysfs_gpu_amd"
    done
fi

# 5. KSM for VM/Docker hosts
if [ "$sysfs_ksm" -eq 1 ]; then
    safe_write /sys/kernel/mm/ksm/run 1
    safe_write /sys/kernel/mm/ksm/pages_to_scan "$sysfs_ksm_pages_to_scan"
    safe_write /sys/kernel/mm/ksm/sleep_millisecs "$sysfs_ksm_sleep_millisecs"
    safe_write /sys/kernel/mm/ksm/merge_across_nodes "$sysfs_ksm_merge_across_nodes"
fi

# 6. Physical Network Interfaces Tuning
if command -v ethtool >/dev/null 2>&1; then
    for eth in /sys/class/net/*; do
        if [ -d "\$eth/device" ] && ! echo "\$eth" | grep -qE 'wireless|wlan'; then
            iface=\$(basename "\$eth")
            ip link set dev "\$iface" txqueuelen $sys_txqueuelen 2>/dev/null || true
            
            if [ "$exp_enabled" -eq 1 ]; then
                # Safe capping to avoid bufferbloat
                max_rx=\$(ethtool -g "\$iface" 2>/dev/null | awk '/Pre-set maximums:/,/RX:/ {if(\$1=="RX:") print \$2}' | tr -d ' ' || echo "")
                max_tx=\$(ethtool -g "\$iface" 2>/dev/null | awk '/Pre-set maximums:/,/TX:/ {if(\$1=="TX:") print \$2}' | tr -d ' ' || echo "")
                
                if [[ "\$max_rx" =~ ^[0-9]+$ ]] && [ "\$max_rx" -gt 0 ]; then
                    [ "\$max_rx" -gt 4096 ] && max_rx=4096
                    ethtool -G "\$iface" rx "\$max_rx" 2>/dev/null || true
                fi
                if [[ "\$max_tx" =~ ^[0-9]+$ ]] && [ "\$max_tx" -gt 0 ]; then
                    [ "\$max_tx" -gt 4096 ] && max_tx=4096
                    ethtool -G "\$iface" tx "\$max_tx" 2>/dev/null || true
                fi
            fi
            
            if [ "$is_server" -eq 1 ]; then
                ethtool -K "\$iface" tso on gso on gro on 2>/dev/null || true
            fi
            
            # RPS (Receive Packet Steering)
            if [ "$is_server" -eq 1 ] || [ "$choice_role" -eq 3 ]; then
                local_node=\$(cat /sys/class/net/"\$iface"/device/numa_node 2>/dev/null || echo -1)
                if [ "$numa_nodes" -eq 1 ] || [ "\$local_node" -eq -1 ]; then
                    mask_val=0
                    for f in /sys/devices/system/cpu/cpu*/topology/thread_siblings_list; do
                        [ -f "\$f" ] || continue
                        first_thread=\$(cut -d, -f1 "\$f" | cut -d- -f1)
                        [ "\$first_thread" -gt 62 ] && continue # Prevent Bash 64-bit signed integer overflow
                        mask_val=\$((mask_val | (1 << first_thread)))
                    done
                    [ "\$mask_val" -eq 0 ] && mask_val=\$(( (1 << \$(nproc 2>/dev/null || echo 1)) - 1 ))
                    mask=\$(printf "%x" "\$mask_val")
                    for rx_queue in /sys/class/net/"\$iface"/queues/rx-*/rps_cpus; do
                        safe_write "\$rx_queue" "\$mask"
                    done
                    for rx_flow in /sys/class/net/"\$iface"/queues/rx-*/rps_flow_cnt; do
                        safe_write "\$rx_flow" "4096"
                    done
                fi
            fi
        fi
    done
fi
EOF
    chmod +x /etc/tuneperf/scripts/apply_sysfs.sh

    cat > /etc/systemd/system/tuneperf-sysfs.service <<EOF
[Unit]
Description=TunePerf SysFS Persistence
After=local-fs.target systemd-modules-load.service

[Service]
Type=oneshot
ExecStart=/etc/tuneperf/scripts/apply_sysfs.sh
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF

    mkdir -p /etc/udev/rules.d
    cat > /etc/udev/rules.d/99-tuneperf-power.rules <<EOF
SUBSYSTEM=="power_supply", ATTR{online}=="0", RUN+="/usr/bin/systemd-run --no-block /etc/tuneperf/scripts/apply_sysfs.sh battery"
SUBSYSTEM=="power_supply", ATTR{online}=="1", RUN+="/usr/bin/systemd-run --no-block /etc/tuneperf/scripts/apply_sysfs.sh ac"
EOF
}

backup_sysctl() {
    local bdir="$BACKUP_DIR/full-$(date +%Y%m%d%H%M%S)"
    mkdir -p "$bdir"
    sysctl -a > "$bdir/sysctl-all.txt" 2>/dev/null || true
    cp -a /etc/sysctl.d "$bdir/sysctl.d.backup" 2>/dev/null || true
    [ -f /etc/default/grub ] && cp /etc/default/grub "$bdir/grub.backup" 2>/dev/null || true
    
    shopt -s nullglob
    for file in "$SYSCTL_DIR"/99-tuneperf-*.conf; do
        cp "$file" "$BACKUP_DIR/$(basename "$file").bak" 2>/dev/null || true
    done
    shopt -u nullglob
    log_to_file "Backup completed at $bdir"
}

handle_conflicting_tools() {
    for tool in tuned tlp powertop irqbalance; do
        if systemctl is-active --quiet "$tool" 2>/dev/null; then
            if [ "$tool" == "irqbalance" ] && [ "$exp_enabled" -eq 0 ]; then
                continue # Only disable irqbalance in experimental mode
            fi
            log_warn "Disabling conflicting tool: $tool"
            systemctl stop "$tool" 2>/dev/null || true
            systemctl disable "$tool" 2>/dev/null || true
            log_to_file "Disabled conflicting tool: $tool"
        fi
    done
}

apply_systemd_limits() {
    if [ "$exp_enabled" -eq 1 ] && command -v systemctl >/dev/null 2>&1; then
        log_warn "Applying massive Systemd limits (MEMLOCK, NOFILE)..."
        mkdir -p /etc/systemd/system.conf.d /etc/systemd/user.conf.d
        
        cat > /etc/systemd/system.conf.d/99-tuneperf-limits.conf <<EOF
[Manager]
DefaultLimitNOFILE=1048576
DefaultLimitMEMLOCK=67108864
EOF
        cp /etc/systemd/system.conf.d/99-tuneperf-limits.conf /etc/systemd/user.conf.d/99-tuneperf-limits.conf
        systemctl daemon-reload 2>/dev/null || true
    fi
}

apply_sysctl() {
    cp /etc/tuneperf/generated/99-tuneperf-swappiness.conf "$SYSCTL_DIR"/
    cp /etc/tuneperf/generated/99-tuneperf-perfs.conf "$SYSCTL_DIR"/
    cp /etc/tuneperf/generated/99-tuneperf-network.conf "$SYSCTL_DIR"/
    cp /etc/tuneperf/generated/99-tuneperf-security.conf "$SYSCTL_DIR"/
    
    if [ -s /etc/tuneperf/generated/tuneperf-modules.conf ]; then
        mkdir -p /etc/modules-load.d
        cp /etc/tuneperf/generated/tuneperf-modules.conf /etc/modules-load.d/tuneperf.conf
        systemctl restart systemd-modules-load.service 2>/dev/null || true
    fi
    
    log_info "Reloading sysctl daemon..."
    sysctl --system >/dev/null 2>&1 || true
    log_succ "Sysctl configuration applied."
    log_to_file "Applied sysctl configurations."
}

apply_sysfs() {
    /etc/tuneperf/scripts/apply_sysfs.sh
    systemctl daemon-reload
    systemctl enable tuneperf-sysfs.service >/dev/null 2>&1
    if command -v udevadm >/dev/null 2>&1; then udevadm control --reload-rules; fi
    log_succ "Sysfs persistence enabled (systemd service & udev battery rules created)."
    log_to_file "Enabled SysFS persistence and udev rules."
}

apply_grub_tweaks() {
    [ -f /etc/default/grub ] || return 0
    local grub_modified=0
    local params=""

    if [ "$exp_enabled" -eq 1 ]; then
        if [ "$has_zram" -eq 1 ]; then
            params="$params zswap.enabled=0"
        fi
        if [ "$choice_role" -eq 3 ]; then
            params="$params split_lock_detect=off"
        fi
    fi

    if [ -n "$params" ]; then
        for p in $params; do
            if ! grep -q "$p" /etc/default/grub; then
                sed -i "s/GRUB_CMDLINE_LINUX_DEFAULT=\"/GRUB_CMDLINE_LINUX_DEFAULT=\"$p /" /etc/default/grub
                grub_modified=1
            fi
        done
    fi

    if [ "$grub_modified" -eq 1 ]; then
        log_info "Updating GRUB with experimental boot parameters ($params)..."
        if command -v grub2-mkconfig >/dev/null 2>&1; then grub2-mkconfig -o /boot/grub2/grub.cfg >/dev/null 2>&1
        elif command -v update-grub >/dev/null 2>&1; then update-grub >/dev/null 2>&1
        elif command -v grub-mkconfig >/dev/null 2>&1; then grub-mkconfig -o /boot/grub/grub.cfg >/dev/null 2>&1
        fi
    fi
}

apply_mitigations_off() {
    if [ "$DISABLE_MITIGATIONS" -eq 1 ] && [ -f /etc/default/grub ]; then
        log_warn "DANGER: Disabling CPU mitigations exposes you to Spectre/Meltdown!"
        if [ "$INTERACTIVE" -eq 1 ]; then
            read -rp "Are you ABSOLUTELY sure you want to disable mitigations? [y/N]: " ack_miti
            if [[ ! "$ack_miti" =~ ^[Yy]$ ]]; then
                log_info "Mitigations will remain ACTIVE."
                return
            fi
        fi
        if ! grep -q "mitigations=off" /etc/default/grub; then
            sed -i 's/GRUB_CMDLINE_LINUX_DEFAULT="/GRUB_CMDLINE_LINUX_DEFAULT="mitigations=off /' /etc/default/grub
            if command -v grub2-mkconfig >/dev/null 2>&1; then grub2-mkconfig -o /boot/grub2/grub.cfg >/dev/null 2>&1
            elif command -v update-grub >/dev/null 2>&1; then update-grub >/dev/null 2>&1
            elif command -v grub-mkconfig >/dev/null 2>&1; then grub-mkconfig -o /boot/grub/grub.cfg >/dev/null 2>&1
            fi
            log_succ "Mitigations disabled in GRUB. A reboot is required to take effect."
            log_to_file "Disabled CPU mitigations in GRUB."
        else
            log_info "Mitigations are already disabled in GRUB."
        fi
    fi
}

restore_config() {
    log_warn "Restoring backups..."
    shopt -s nullglob
    
    rm -f "$SYSCTL_DIR"/99-tuneperf-*.conf
    rm -f /etc/modules-load.d/tuneperf.conf
    rm -f /etc/systemd/system.conf.d/99-tuneperf-limits.conf
    rm -f /etc/systemd/user.conf.d/99-tuneperf-limits.conf
    
    # Restore GRUB
    if [ -f "$backup_dir/grub.backup" ]; then
        cp "$backup_dir/grub.backup" /etc/default/grub
        if command -v grub2-mkconfig >/dev/null 2>&1; then grub2-mkconfig -o /boot/grub2/grub.cfg >/dev/null 2>&1
        elif command -v update-grub >/dev/null 2>&1; then update-grub >/dev/null 2>&1
        elif command -v grub-mkconfig >/dev/null 2>&1; then grub-mkconfig -o /boot/grub/grub.cfg >/dev/null 2>&1
        fi
    elif grep -q "mitigations=off" /etc/default/grub 2>/dev/null; then
        sed -i 's/mitigations=off //g' /etc/default/grub
        if command -v grub2-mkconfig >/dev/null 2>&1; then grub2-mkconfig -o /boot/grub2/grub.cfg >/dev/null 2>&1
        elif command -v update-grub >/dev/null 2>&1; then update-grub >/dev/null 2>&1
        elif command -v grub-mkconfig >/dev/null 2>&1; then grub-mkconfig -o /boot/grub/grub.cfg >/dev/null 2>&1
        fi
    fi
    
    for file in "$BACKUP_DIR"/99-tuneperf-*.conf.bak; do
        target="$SYSCTL_DIR/$(basename "$file" .bak)"
        cp "$file" "$target"
    done
    shopt -u nullglob
    
    sysctl --system >/dev/null 2>&1 || true
    systemctl disable tuneperf-sysfs.service 2>/dev/null || true
    rm -f /etc/systemd/system/tuneperf-sysfs.service
    rm -f /etc/udev/rules.d/99-tuneperf-power.rules
    systemctl daemon-reload
    if command -v udevadm >/dev/null 2>&1; then udevadm control --reload-rules; fi
    
    log_succ "Restored config, disabled sysfs persistence and removed udev rules."
    log_to_file "Restored configuration to previous state."
    exit 0
}

# --- MAIN RUNNER ---
exec 9>/var/lock/tuneperf.lock
if ! flock -n 9; then
    log_err "Another instance of TunePerf is already running."
    exit 1
fi
trap 'rm -f /var/lock/tuneperf.lock' EXIT

for cmd in sysctl awk grep lscpu nproc df findmnt; do
    require_cmd "$cmd"
done

if [ "$EUID" -ne 0 ]; then
  echo -e "${RED}This script must be run as root (or with sudo).${NC}"
  exit 1
fi

parse_args "$@"

if [ "$RESTORE" -eq 1 ]; then
    restore_config
fi

detect_hardware

if [ "$INTERACTIVE" -eq 1 ]; then
    ask_profile
else
    load_profile
fi

calculate_parameters
generate_sysctl
generate_systemd_persistence

if [ "$DRY_RUN" -eq 1 ]; then
    log_info "DRY RUN MODE. Generated configs in /etc/tuneperf/generated/."
    cat /etc/tuneperf/generated/99-tuneperf-swappiness.conf
    cat /etc/tuneperf/generated/99-tuneperf-perfs.conf
    cat /etc/tuneperf/generated/99-tuneperf-network.conf
    cat /etc/tuneperf/generated/99-tuneperf-security.conf
    cat /etc/tuneperf/scripts/apply_sysfs.sh
    exit 0
fi

if [ "$INTERACTIVE" -eq 1 ]; then
    read -rp "Preview generated config before applying? [y/N]: " preview_conf
    if [[ "$preview_conf" =~ ^[Yy]$ ]]; then
        cat /etc/tuneperf/generated/99-tuneperf-swappiness.conf
        cat /etc/tuneperf/generated/99-tuneperf-perfs.conf
        cat /etc/tuneperf/generated/99-tuneperf-network.conf
        cat /etc/tuneperf/generated/99-tuneperf-security.conf
    fi

    read -rp "Do you want to apply sysctl and enable sysfs persistence now? [y/N]: " apply_now
    if [[ ! "$apply_now" =~ ^[Yy]$ ]]; then
        log_info "Operation aborted by user."
        exit 0
    fi
fi

handle_conflicting_tools
apply_systemd_limits
backup_sysctl
apply_sysctl
apply_sysfs
apply_mitigations_off
apply_grub_tweaks

log_succ "All tunings applied successfully and made persistent!"
