#include "tuneperf_runner.h"
#include "tuneperf_sh.h"
#include <ntqfile.h>
#include <ntqtextstream.h>
#include <ntqdir.h>
#include <ntqapplication.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

TunePerfRunner::TunePerfRunner(TQObject* parent, const TQString& adminPassword)
    : TQObject(parent), m_currentAction(0), m_tempScriptPath(""), m_adminPassword(adminPassword)
{
    m_process = new TQtProcess(this);
    connect(m_process, SIGNAL(finished(int)), this, SLOT(onProcessFinished(int)));
    connect(m_process, SIGNAL(error(const TQString&)), this, SLOT(onProcessError(const TQString&)));
    connect(m_process, SIGNAL(readyReadStandardOutput(const TQByteArray&)), this, SLOT(onProcessStdout(const TQByteArray&)));
    connect(m_process, SIGNAL(readyReadStandardError(const TQByteArray&)), this, SLOT(onProcessStderr(const TQByteArray&)));
}

TunePerfRunner::~TunePerfRunner()
{
    if (!m_tempScriptPath.isEmpty()) {
        unlink(m_tempScriptPath.latin1());
        m_tempScriptPath = "";
    }
}

TQString TunePerfRunner::getOrExtractScript()
{
    if (!m_tempScriptPath.isEmpty() && TQFile::exists(m_tempScriptPath)) {
        return m_tempScriptPath;
    }
    
    char tempTemplate[] = "/tmp/tuneperf_XXXXXX";
    int fd = mkstemp(tempTemplate);
    if (fd >= 0) {
        write(fd, tuneperf_sh, tuneperf_sh_len);
        close(fd);
        chmod(tempTemplate, 0755); // make it executable
        m_tempScriptPath = tempTemplate;
        return m_tempScriptPath;
    }
    
    return "";
}

void TunePerfRunner::writeProfile(const TQString& role, const TQString& usage, const TQString& ipv6, bool exp, bool cstates)
{
    TQDir().mkdir("/tmp/tuneperf");
    TQFile file("/tmp/tuneperf/profile.conf");
    
    if (file.open(IO_WriteOnly | IO_Truncate)) {
        TQTextStream ts(&file);
        ts << "choice_role=\"" << role << "\"\n";
        ts << "choice_usage=\"" << usage << "\"\n";
        ts << "choice_ipv6=\"" << ipv6 << "\"\n";
        ts << "exp_enabled=\"" << (exp ? "1" : "0") << "\"\n";
        ts << "cstates_disabled=\"" << (cstates ? "1" : "0") << "\"\n";
        file.close();
        
        TQString cmd = "sudo -S -p '' sh -c 'mkdir -p /etc/tuneperf && cp /tmp/tuneperf/profile.conf /etc/tuneperf/profile.conf' 2>/dev/null";
        FILE* fp = popen(cmd.latin1(), "w");
        if (fp) {
            TQString pass = m_adminPassword + "\n";
            fwrite(pass.local8Bit().data(), 1, pass.length(), fp);
            pclose(fp);
        }
    }
}

void TunePerfRunner::runDryRun()
{
    if (m_process->isRunning()) return;
    
    TQString scriptPath = getOrExtractScript();
    if (scriptPath.isEmpty()) {
        emit logMessage("Error: Could not extract tuneperf.sh script!\n");
        emit dryRunFinished(false);
        return;
    }
    
    m_currentAction = 1;
    m_hardwareInfo.clear();
    m_dryRunOutput = TQByteArray();
    emit logMessage("Starting hardware detection and dry-run parameter generation...\n");
    TQStringList sudoArgs;
    sudoArgs << "-S" << "-p" << "" << scriptPath << "--dry-run" << "--gui";
    m_process->start("sudo", sudoArgs);
    TQString passStr = m_adminPassword + "\n";
    m_process->write(passStr.local8Bit());
}

void TunePerfRunner::applyTuning(bool disableMitigations)
{
    if (m_process->isRunning()) return;
    
    TQString scriptPath = getOrExtractScript();
    if (scriptPath.isEmpty()) {
        emit logMessage("Error: Could not extract tuneperf.sh script!\n");
        emit applyFinished(false);
        return;
    }
    
    // Save current parameters to /etc/tuneperf/generated/ first
    if (!saveConfigs()) {
        emit logMessage("Warning: Failed to save updated configuration files prior to apply.\n");
    }
    
    m_currentAction = 2;
    emit logMessage("Applying optimized parameters to the system...\n");
    
    TQStringList sudoArgs;
    sudoArgs << "-S" << "-p" << "" << scriptPath << "--apply-only" << "--gui";
    if (disableMitigations) {
        sudoArgs << "--disable-mitigations";
    }
    m_process->start("sudo", sudoArgs);
    TQString passStr = m_adminPassword + "\n";
    m_process->write(passStr.local8Bit());
}

void TunePerfRunner::runRestore()
{
    if (m_process->isRunning()) return;
    
    TQString scriptPath = getOrExtractScript();
    if (scriptPath.isEmpty()) {
        emit logMessage("Error: Could not extract tuneperf.sh script!\n");
        emit restoreFinished(false);
        return;
    }
    
    m_currentAction = 3;
    emit logMessage("Restoring original system settings...\n");
    TQStringList sudoArgs;
    sudoArgs << "-S" << "-p" << "" << scriptPath << "--restore" << "--gui";
    m_process->start("sudo", sudoArgs);
    TQString passStr = m_adminPassword + "\n";
    m_process->write(passStr.local8Bit());
}

void TunePerfRunner::onProcessFinished(int exitCode)
{
    bool success = (exitCode == 0);
    
    if (m_currentAction == 1) { // DryRun
        if (success) {
            parseHardwareInfo(m_dryRunOutput);
            loadConfigs();
        }
        m_currentAction = 0;
        emit dryRunFinished(success);
    } 
    else if (m_currentAction == 2) { // Apply
        m_currentAction = 0;
        emit applyFinished(success);
    } 
    else if (m_currentAction == 3) { // Restore
        m_currentAction = 0;
        emit restoreFinished(success);
    }
}

void TunePerfRunner::onProcessError(const TQString& message)
{
    emit logMessage("Process Error: " + message + "\n");
    
    if (m_currentAction == 1) {
        m_currentAction = 0;
        emit dryRunFinished(false);
    } else if (m_currentAction == 2) {
        m_currentAction = 0;
        emit applyFinished(false);
    } else if (m_currentAction == 3) {
        m_currentAction = 0;
        emit restoreFinished(false);
    }
}

void TunePerfRunner::parseHardwareInfo(const TQByteArray& output)
{
    TQString outStr = TQString::fromUtf8(output.data(), output.size());
    TQStringList lines = TQStringList::split('\n', outStr);
    for (TQStringList::ConstIterator it = lines.begin(); it != lines.end(); ++it) {
        TQString line = *it;
        if (line.contains(" - ")) {
            m_hardwareInfo.append(line.mid(line.find(" - ") + 3).stripWhiteSpace());
        }
    }
}

TuneParamList TunePerfRunner::parseConfFile(const TQString& filePath)
{
    TuneParamList list;
    TQFile file(filePath);
    if (!file.open(IO_ReadOnly)) return list;
    
    TQTextStream ts(&file);
    ts.setEncoding(TQTextStream::UnicodeUTF8);
    TQString currentComment;
    bool isModules = filePath.contains("tuneperf-modules.conf");
    
    while (!ts.atEnd()) {
        TQString line = ts.readLine().stripWhiteSpace();
        if (line.isEmpty()) continue;
        
        if (line.startsWith("#") || line.startsWith(";")) {
            TQString content = line.mid(1).stripWhiteSpace();
            if (content.contains("Generated by TunePerf") || content.contains("Generated and edited")) {
                continue;
            }
            if (isModules && !content.contains(' ') && !content.isEmpty()) {
                // Disabled module
                TuneParam p;
                p.key = content;
                p.value = "";
                p.enabled = false;
                p.comment = currentComment;
                bool isLoaded = TQDir(TQString("/sys/module/") + p.key).exists();
                p.currentValue = isLoaded ? "Active" : "Inactive";
                list.append(p);
                currentComment = "";
            } else {
                if (!currentComment.isEmpty()) currentComment += "\n";
                currentComment += content;
            }
        } else {
            if (isModules) {
                TuneParam p;
                p.key = line;
                p.value = "";
                p.enabled = true;
                p.comment = currentComment;
                bool isLoaded = TQDir(TQString("/sys/module/") + p.key).exists();
                p.currentValue = isLoaded ? "Active" : "Inactive";
                list.append(p);
                currentComment = "";
            } else {
                int eqIdx = line.find('=');
                if (eqIdx > 0) {
                    TuneParam p;
                    p.key = line.left(eqIdx).stripWhiteSpace();
                    p.value = line.mid(eqIdx + 1).stripWhiteSpace();
                    p.comment = currentComment;
                    p.enabled = true;
                    
                    // Read current sysctl value from /proc/sys
                    TQString sysctlPath = "/proc/sys/" + p.key;
                    sysctlPath.replace('.', '/');
                    TQFile sysctlFile(sysctlPath);
                    if (sysctlFile.open(IO_ReadOnly)) {
                        TQTextStream sts(&sysctlFile);
                        TQString curVal = sts.readLine().stripWhiteSpace();
                        sysctlFile.close();
                        if (!curVal.isEmpty()) {
                            p.currentValue = curVal;
                        }
                    }
                    
                    list.append(p);
                    currentComment = "";
                }
            }
        }
    }
    file.close();
    return list;
}

bool TunePerfRunner::writeConfFile(const TQString& filePath, const TuneParamList& list)
{
    TQFile file(filePath);
    if (!file.open(IO_WriteOnly | IO_Truncate)) return false;
    
    TQTextStream ts(&file);
    ts.setEncoding(TQTextStream::UnicodeUTF8);
    ts << "# Generated and edited by TunePerf GUI\n";
    
    bool isModules = filePath.contains("tuneperf-modules.conf");
    
    for (TuneParamList::ConstIterator it = list.begin(); it != list.end(); ++it) {
        const TuneParam& p = *it;
        if (!p.comment.isEmpty()) {
            TQStringList commentLines = TQStringList::split('\n', p.comment);
            for (TQStringList::ConstIterator cit = commentLines.begin(); cit != commentLines.end(); ++cit) {
                ts << "# " << *cit << "\n";
            }
        }
        if (!p.enabled) {
            ts << "# ";
        }
        if (isModules) {
            ts << p.key << "\n";
        } else {
            ts << p.key << "=" << p.value << "\n\n";
        }
    }
    file.close();
    return true;
}

static bool findModuleFile(const TQDir& dir, const TQString& pattern) {
    TQStringList entries = dir.entryList(TQDir::All);
    for (TQStringList::ConstIterator it = entries.begin(); it != entries.end(); ++it) {
        TQString name = *it;
        if (name == "." || name == "..") continue;
        TQString fullPath = dir.filePath(name);
        if (name.contains(pattern) && (name.endsWith(".ko") || name.endsWith(".ko.xz") || name.endsWith(".ko.zst") || name.endsWith(".ko.gz"))) {
            return true;
        }
        TQDir sub(fullPath);
        if (sub.exists()) {
            if (findModuleFile(sub, pattern)) return true;
        }
    }
    return false;
}

static bool isModuleSupported(const TQString& moduleName) {
    TQString osRelease = "";
    TQFile releaseFile("/proc/sys/kernel/osrelease");
    if (releaseFile.open(IO_ReadOnly)) {
        TQTextStream ts(&releaseFile);
        osRelease = ts.readLine().stripWhiteSpace();
        releaseFile.close();
    }
    if (osRelease.isEmpty()) return false;
    
    TQDir modDir("/lib/modules/" + osRelease + "/kernel");
    if (!modDir.exists()) {
        modDir = TQDir("/lib/modules/" + osRelease);
    }
    if (!modDir.exists()) return false;
    return findModuleFile(modDir, moduleName);
}

static TQString resolveSysfsPathAndRead(const TQString& command) {
    if (command.contains("modprobe bfq")) {
        if (TQDir("/sys/module/bfq").exists()) {
            return "loaded";
        }
        if (isModuleSupported("bfq")) {
            return "not loaded";
        }
        return "Unsupported";
    }
    
    if (command.contains("nvidia-smi")) {
        bool hasNvidia = TQDir("/proc/driver/nvidia").exists();
        if (!hasNvidia) {
            TQString path = getenv("PATH");
            TQStringList dirs = TQStringList::split(":", path);
            for (TQStringList::ConstIterator it = dirs.begin(); it != dirs.end(); ++it) {
                if (TQFile::exists(*it + "/nvidia-smi")) {
                    hasNvidia = true;
                    break;
                }
            }
        }
        
        if (!hasNvidia) {
            return "Unsupported";
        }
        
        TQtProcess proc;
        proc.start("nvidia-smi", TQStringList() << "--query-gpu=persistence_mode" << "--format=csv,noheader");
        if (proc.waitForFinished(1000)) {
            TQByteArray out = proc.readAllStandardOutput();
            TQString val = TQString::fromUtf8(out.data(), out.size()).stripWhiteSpace();
            if (!val.isEmpty()) {
                return val;
            }
        }
        return "Disabled";
    }

    int sysIdx = command.find("/sys/");
    if (sysIdx == -1) sysIdx = command.find("/proc/");
    if (sysIdx == -1) return "";
    
    TQString path;
    int i = sysIdx;
    while (i < command.length()) {
        TQChar c = command[i];
        if (c == ' ' || c == '\t' || c == '"' || c == '\'') {
            break;
        }
        path.append(c);
        i++;
    }
    
    if (path.isEmpty()) return "";
    
    if (path.contains("amd-pstate")) {
        if (!TQDir("/sys/devices/system/cpu/amd-pstate").exists()) {
            return "Unsupported";
        }
    }
    
    // Resolve variables
    if (path.contains("$block_dev") || path.contains("block_dev")) {
        if (command.contains("zram")) {
            if (TQDir("/sys/block/zram0").exists()) {
                path.replace("$block_dev", "/sys/block/zram0");
                path.replace("\"$block_dev\"", "/sys/block/zram0");
            }
        } else {
            TQString firstDev = "";
            TQDir d("/sys/block");
            TQStringList entries = d.entryList("sd*", TQDir::Dirs);
            if (entries.isEmpty()) entries = d.entryList("nvme*", TQDir::Dirs);
            if (entries.isEmpty()) entries = d.entryList("vd*", TQDir::Dirs);
            if (!entries.isEmpty()) {
                firstDev = "/sys/block/" + entries[0];
            } else {
                firstDev = "/sys/block/sda"; // fallback
            }
            path.replace("$block_dev", firstDev);
            path.replace("\"$block_dev\"", firstDev);
        }
    }
    
    if (path.contains("$policy") || path.contains("policy")) {
        TQDir d("/sys/devices/system/cpu/cpufreq");
        TQStringList entries = d.entryList("policy*", TQDir::Dirs);
        TQString firstPolicy = "/sys/devices/system/cpu/cpufreq/policy0";
        if (!entries.isEmpty()) {
            firstPolicy = "/sys/devices/system/cpu/cpufreq/" + entries[0];
        }
        path.replace("$policy", firstPolicy);
        path.replace("\"$policy\"", firstPolicy);
    }
    
    TQFile file(path);
    if (file.open(IO_ReadOnly)) {
        TQTextStream ts(&file);
        TQString val = ts.readLine().stripWhiteSpace();
        file.close();
        
        if (val.contains('[') && val.contains(']')) {
            int start = val.find('[');
            int end = val.find(']');
            if (start != -1 && end != -1 && end > start) {
                val = val.mid(start + 1, end - start - 1);
            }
        }
        return val;
    }
    
    return "";
}

ScriptLineList TunePerfRunner::parseScriptFile(const TQString& filePath)
{
    ScriptLineList list;
    TQFile file(filePath);
    if (!file.open(IO_ReadOnly)) return list;
    
    TQTextStream ts(&file);
    ts.setEncoding(TQTextStream::UnicodeUTF8);
    
    while (!ts.atEnd()) {
        TQString line = ts.readLine();
        TQString trimmed = line.stripWhiteSpace();
        
        ScriptLine s;
        s.text = line;
        s.isCommand = false;
        s.enabled = true;
        
        TQString checkCmd = trimmed;
        bool isCommented = false;
        if (trimmed.startsWith("#")) {
            checkCmd = trimmed.mid(1).stripWhiteSpace();
            isCommented = true;
        }
        
        // Match executable lines
        bool isStaticCmd = false;
        if (checkCmd.startsWith("safe_write /") || checkCmd.startsWith("safe_write \"/")) {
            isStaticCmd = true;
        } else if (checkCmd.startsWith("nvidia-smi")) {
            isStaticCmd = true;
        } else if (checkCmd.startsWith("modprobe ") && !checkCmd.contains("$")) {
            isStaticCmd = true;
        }
        
        if (isStaticCmd) {
            s.isCommand = true;
            s.enabled = !isCommented;
            
            // Map to descriptive labels
            if (checkCmd.contains("base_slice_ns")) {
                s.label = "EEVDF Scheduler base slice time";
            } else if (checkCmd.contains("transparent_hugepage/enabled")) {
                s.label = "Transparent Huge Pages (THP)";
            } else if (checkCmd.contains("transparent_hugepage/defrag")) {
                s.label = "THP Defragmentation policy";
            } else if (checkCmd.contains("transparent_hugepage/khugepaged")) {
                s.label = "Khugepaged daemon defrag sleep cycle";
            } else if (checkCmd.contains("comp_algorithm")) {
                s.label = "ZRAM Compression Algorithm (zstd)";
            } else if (checkCmd.contains("max_comp_streams")) {
                s.label = "ZRAM CPU Compression Streams scaling";
            } else if (checkCmd.contains("read_ahead_kb")) {
                s.label = "Block devices I/O Read-Ahead Buffer size";
            } else if (checkCmd.contains("nr_requests")) {
                s.label = "Block devices maximum I/O request size";
            } else if (checkCmd.contains("nomerges")) {
                s.label = "Disable I/O Merges for NVMe drives";
            } else if (checkCmd.contains("add_random")) {
                s.label = "Disable disk entropy accumulation (reduces latency)";
            } else if (checkCmd.contains("scaling_governor")) {
                s.label = "CPU Scaling Governor";
            } else if (checkCmd.contains("energy_performance_preference")) {
                s.label = "CPU Energy Performance Preference (EPP)";
            } else if (checkCmd.contains("amd-pstate/status")) {
                s.label = "Enable AMD P-State Active Mode";
            } else if (checkCmd.contains("pcie_aspm")) {
                s.label = "Force PCIe ASPM to maximum performance";
            } else if (checkCmd.contains("nvidia-smi")) {
                s.label = "Nvidia GPU Persistence Mode (keeps GPU awake)";
            } else if (checkCmd.contains("ksm/run")) {
                s.label = "Kernel Samepage Merging (KSM) for virtualization";
            } else if (checkCmd.contains("txqueuelen")) {
                s.label = "Network Interface Transmit Queue Length (txqueuelen)";
            } else if (checkCmd.contains("ethtool -G")) {
                s.label = "Ethtool Ring Buffer optimization (reduces packet drops)";
            } else if (checkCmd.contains("tso on")) {
                s.label = "TCP Segmentation Offload (TSO/GSO/GRO)";
            } else if (checkCmd.contains("rps_cpus")) {
                s.label = "Receive Packet Steering (RPS) multicore mapping";
            } else if (checkCmd.contains("modprobe bfq")) {
                s.label = "Enable BFQ I/O Scheduler kernel module";
            } else {
                s.label = checkCmd; // fallback
            }
            
            // Read active sysfs value
            s.currentValue = resolveSysfsPathAndRead(checkCmd);
        }
        list.append(s);
    }
    file.close();
    return list;
}

bool TunePerfRunner::writeScriptFile(const TQString& filePath, const ScriptLineList& lines)
{
    if (lines.isEmpty()) return true; // Safeguard against empty lists
    
    TQFile file(filePath);
    if (!file.open(IO_WriteOnly | IO_Truncate)) return false;
    
    TQTextStream ts(&file);
    ts.setEncoding(TQTextStream::UnicodeUTF8);
    
    for (ScriptLineList::ConstIterator it = lines.begin(); it != lines.end(); ++it) {
        const ScriptLine& s = *it;
        if (s.isCommand) {
            TQString checkCmd = s.text.stripWhiteSpace();
            bool alreadyCommented = checkCmd.startsWith("#");
            
            if (s.enabled && alreadyCommented) {
                // Strip the comment sign
                TQString uncommented = checkCmd.mid(1).stripWhiteSpace();
                ts << uncommented << "\n";
            } else if (!s.enabled && !alreadyCommented) {
                // Add the comment sign
                ts << "# " << checkCmd << "\n";
            } else {
                ts << s.text << "\n";
            }
        } else {
            ts << s.text << "\n";
        }
    }
    file.close();
    return true;
}

bool TunePerfRunner::loadConfigs()
{
    m_swappinessParams = parseConfFile("/etc/tuneperf/generated/99-tuneperf-swappiness.conf");
    m_perfsParams = parseConfFile("/etc/tuneperf/generated/99-tuneperf-perfs.conf");
    m_networkParams = parseConfFile("/etc/tuneperf/generated/99-tuneperf-network.conf");
    m_securityParams = parseConfFile("/etc/tuneperf/generated/99-tuneperf-security.conf");
    m_modulesParams = parseConfFile("/etc/tuneperf/generated/tuneperf-modules.conf");
    m_logsParams = parseConfFile("/etc/tuneperf/generated/tuneperf-logs.conf");
    m_sysfsScriptLines = parseScriptFile("/etc/tuneperf/scripts/apply_sysfs.sh");
    return true;
}

bool TunePerfRunner::saveConfigs()
{
    TQDir().mkdir("/tmp/tuneperf");
    TQDir().mkdir("/tmp/tuneperf/generated");
    TQDir().mkdir("/tmp/tuneperf/scripts");
    
    bool ok = true;
    ok &= writeConfFile("/tmp/tuneperf/generated/99-tuneperf-swappiness.conf", m_swappinessParams);
    ok &= writeConfFile("/tmp/tuneperf/generated/99-tuneperf-perfs.conf", m_perfsParams);
    ok &= writeConfFile("/tmp/tuneperf/generated/99-tuneperf-network.conf", m_networkParams);
    ok &= writeConfFile("/tmp/tuneperf/generated/99-tuneperf-security.conf", m_securityParams);
    ok &= writeConfFile("/tmp/tuneperf/generated/tuneperf-modules.conf", m_modulesParams);
    ok &= writeConfFile("/tmp/tuneperf/generated/tuneperf-logs.conf", m_logsParams);
    ok &= writeScriptFile("/tmp/tuneperf/scripts/apply_sysfs.sh", m_sysfsScriptLines);
    
    if (!ok) return false;
    
    TQString cmd = "sudo -S -p '' sh -c 'mkdir -p /etc/tuneperf/generated /etc/tuneperf/scripts && cp -r /tmp/tuneperf/* /etc/tuneperf/' 2>/dev/null";
    FILE* fp = popen(cmd.latin1(), "w");
    if (fp) {
        TQString pass = m_adminPassword + "\n";
        fwrite(pass.local8Bit().data(), 1, pass.length(), fp);
        int status = pclose(fp);
        return (WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    return false;
}

bool TunePerfRunner::hasBackup() const
{
    TQDir dir("/etc/tuneperf/backup");
    if (!dir.exists()) return false;
    TQStringList entries = dir.entryList(TQDir::All);
    for (TQStringList::ConstIterator it = entries.begin(); it != entries.end(); ++it) {
        if (*it != "." && *it != "..") return true;
    }
    return false;
}

TQString TunePerfRunner::getLogValue(const TQString& key) const
{
    for (TuneParamList::ConstIterator it = m_logsParams.begin(); it != m_logsParams.end(); ++it) {
        if ((*it).key == key) return (*it).value;
    }
    return "0";
}

void TunePerfRunner::setLogValue(const TQString& key, const TQString& val)
{
    for (TuneParamList::Iterator it = m_logsParams.begin(); it != m_logsParams.end(); ++it) {
        if ((*it).key == key) {
            (*it).value = val;
            return;
        }
    }
    TuneParam p;
    p.key = key;
    p.value = val;
    p.enabled = true;
    m_logsParams.append(p);
}

void TunePerfRunner::readProfile(TQString& role, TQString& usage, TQString& ipv6, bool& exp, bool& cstates)
{
    role = "1";
    usage = "1";
    ipv6 = "1";
    exp = false;
    cstates = false;
    
    TQFile file("/etc/tuneperf/profile.conf");
    if (!file.open(IO_ReadOnly)) return;
    
    TQTextStream ts(&file);
    while (!ts.atEnd()) {
        TQString line = ts.readLine().stripWhiteSpace();
        if (line.startsWith("#") || !line.contains("=")) continue;
        int eqIdx = line.find('=');
        if (eqIdx == -1) continue;
        TQString key = line.left(eqIdx).stripWhiteSpace();
        TQString val = line.mid(eqIdx + 1).stripWhiteSpace();
        
        if (key == "ROLE") role = val;
        else if (key == "USAGE") usage = val;
        else if (key == "IPV6") ipv6 = val;
        else if (key == "EXPERIMENTAL") exp = (val == "1");
        else if (key == "CSTATES") cstates = (val == "1");
    }
    file.close();
}

void TunePerfRunner::onProcessStdout(const TQByteArray& data)
{
    if (m_currentAction == 1) { // DryRun
        int oldSize = m_dryRunOutput.size();
        m_dryRunOutput.resize(oldSize + data.size());
        memcpy(m_dryRunOutput.data() + oldSize, data.data(), data.size());
    } else {
        TQString str = TQString::fromUtf8(data.data(), data.size());
        if (!str.isEmpty()) {
            emit logMessage(str);
        }
    }
}

void TunePerfRunner::onProcessStderr(const TQByteArray& data)
{
    if (m_currentAction != 1) {
        TQString str = TQString::fromUtf8(data.data(), data.size());
        if (!str.isEmpty()) {
            emit logMessage(str);
        }
    }
}

#include "tuneperf_runner.moc"
