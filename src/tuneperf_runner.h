#ifndef TUNEPERF_RUNNER_H
#define TUNEPERF_RUNNER_H

#include <ntqobject.h>
#include <ntqstring.h>
#include <ntqstringlist.h>
#include <ntqvaluelist.h>
#include "tqtprocess.h"

struct TuneParam {
    TQString key;
    TQString value;
    TQString comment;
    bool enabled;
    TQString currentValue;
};

struct ScriptLine {
    TQString text;
    TQString label;
    bool isCommand;
    bool enabled;
    TQString currentValue;
};

typedef TQValueList<TuneParam> TuneParamList;
typedef TQValueList<ScriptLine> ScriptLineList;

class TunePerfRunner : public TQObject {
    TQ_OBJECT
public:
    TunePerfRunner(TQObject* parent = 0, const TQString& adminPassword = "");
    ~TunePerfRunner();

    // Getters for parsed details
    TQStringList hardwareInfo() const { return m_hardwareInfo; }
    TuneParamList swappinessParams() const { return m_swappinessParams; }
    TuneParamList perfsParams() const { return m_perfsParams; }
    TuneParamList networkParams() const { return m_networkParams; }
    TuneParamList securityParams() const { return m_securityParams; }
    TuneParamList modulesParams() const { return m_modulesParams; }
    ScriptLineList sysfsScriptLines() const { return m_sysfsScriptLines; }

    // Setters for updated parameters
    void setSwappinessParams(const TuneParamList& params) { m_swappinessParams = params; }
    void setPerfsParams(const TuneParamList& params) { m_perfsParams = params; }
    void setNetworkParams(const TuneParamList& params) { m_networkParams = params; }
    void setSecurityParams(const TuneParamList& params) { m_securityParams = params; }
    void setModulesParams(const TuneParamList& params) { m_modulesParams = params; }
    void setSysfsScriptLines(const ScriptLineList& lines) { m_sysfsScriptLines = lines; }

    // Runner API
    void runDryRun();
    void applyTuning(bool disableMitigations);
    void runRestore();
    void writeProfile(const TQString& role, const TQString& usage, const TQString& ipv6, bool exp, bool cstates);
    void readProfile(TQString& role, TQString& usage, TQString& ipv6, bool& exp, bool& cstates);
    bool hasBackup() const;

    // Log values
    TQString getLogValue(const TQString& key) const;
    void setLogValue(const TQString& key, const TQString& val);

    // Config IO
    bool loadConfigs();
    bool saveConfigs();

signals:
    void dryRunFinished(bool success);
    void applyFinished(bool success);
    void restoreFinished(bool success);
    void logMessage(const TQString& msg);

private slots:
    void onProcessFinished(int exitCode);
    void onProcessError(const TQString& message);
    void onProcessStdout(const TQByteArray& data);
    void onProcessStderr(const TQByteArray& data);

private:
    void parseHardwareInfo(const TQByteArray& output);
    TuneParamList parseConfFile(const TQString& filePath);
    bool writeConfFile(const TQString& filePath, const TuneParamList& list);
    
    ScriptLineList parseScriptFile(const TQString& filePath);
    bool writeScriptFile(const TQString& filePath, const ScriptLineList& lines);

    TQtProcess* m_process;
    int m_currentAction; // 0 = Idle, 1 = DryRun, 2 = Apply, 3 = Restore

    TQString m_tempScriptPath;
    TQString getOrExtractScript();

    TQStringList m_hardwareInfo;
    TQByteArray m_dryRunOutput;
    
    // Configurations
    TuneParamList m_swappinessParams;
    TuneParamList m_perfsParams;
    TuneParamList m_networkParams;
    TuneParamList m_securityParams;
    TuneParamList m_modulesParams;
    ScriptLineList m_sysfsScriptLines;
    TuneParamList m_logsParams;
    TQString m_adminPassword;
};

#endif // TUNEPERF_RUNNER_H
