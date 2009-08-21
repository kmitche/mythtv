/**
 *  HDHRChannel
 *  Copyright (c) 2006-2009 by Silicondust Engineering Ltd.
 *  Distributed as part of MythTV under GPL v2 and later.
 */

// C includes
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <fcntl.h>

// C++ includes
#include <algorithm>
using namespace std;

// MythTV includes
#include "mythdbcon.h"
#include "mythcontext.h"
#include "hdhrchannel.h"
#include "videosource.h"
#include "channelutil.h"

#define DEBUG_PID_FILTERS

#define LOC QString("HDHRChan(%1): ").arg(GetDevice())
#define LOC_ERR QString("HDHRChan(%1), Error: ").arg(GetDevice())

HDHRChannel::HDHRChannel(TVRec *parent, const QString &device, uint tuner)
    : DTVChannel(parent),       _hdhomerun_device(NULL),
      _device_id(0),            _device_ip(0),
      _tuner(tuner),            _lock(true)
{
    bool valid;
    _device_id = device.toUInt(&valid, 16);

    if (valid && hdhomerun_discover_validate_device_id(_device_id))
	return;

    _device_id = HDHOMERUN_DEVICE_ID_WILDCARD;

    /* Otherwise, is it a valid IP address? */
    struct in_addr address;
    if (inet_aton(device, &address)) 
    {
	_device_ip = ntohl(address.s_addr);
	return;
    }

    /* Invalid, use wildcard device ID. */
    VERBOSE(VB_IMPORTANT, LOC_ERR + QString("Invalid DeviceID '%1'")
	    .arg(device));

}

HDHRChannel::~HDHRChannel(void)
{
    Close();
}

bool HDHRChannel::Open(void)
{
    if (IsOpen())
        return true;

    if (!InitializeInputs())
        return false;

    return Connect();
}

void HDHRChannel::Close(void)
{
    if (_hdhomerun_device)
    {
        hdhomerun_device_destroy(_hdhomerun_device);
        _hdhomerun_device = NULL;
    }
}

bool HDHRChannel::EnterPowerSavingMode(void)
{
    return hdhomerun_device_set_tuner_channel(_hdhomerun_device, "none") > 0;
}

bool HDHRChannel::Connect(void)
{
    _hdhomerun_device = hdhomerun_device_create(
        _device_id, _device_ip, _tuner, NULL);

    if (!_hdhomerun_device)
    {
        VERBOSE(VB_IMPORTANT,
                LOC_ERR + "Unable to create hdhomerun device object");
        return false;
    }

    return true;
}

QString HDHRChannel::DeviceGet(const QString &name, bool report_error_return)
{
    QMutexLocker locker(&_lock);

    if (!_hdhomerun_device)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "Get request failed (not connected)");
        return QString::null;
    }

    char *value = NULL;
    char *error = NULL;
    if (hdhomerun_device_get_var(_hdhomerun_device, name, &value, &error) < 0)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "Get request failed" + ENO);
        return QString::null;
    }

    if (report_error_return && error)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                QString("DeviceGet(%1): %2").arg(name).arg(error));

        return QString::null;
    }

    return QString(value);
}

QString HDHRChannel::DeviceSet(const QString &name, const QString &val,
                               bool report_error_return)
{
    QMutexLocker locker(&_lock);

    if (!_hdhomerun_device)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "Set request failed (not connected)");
        return QString::null;
    }

    char *value = NULL;
    char *error = NULL;
    if (hdhomerun_device_set_var(
            _hdhomerun_device, name, val, &value, &error) < 0)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "Set request failed" + ENO);

        return QString::null;
    }

    if (report_error_return && error)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                QString("DeviceSet(%1 %2): %3").arg(name).arg(val).arg(error));

        return QString::null;
    }

    return QString(value);
}

struct hdhomerun_device_t *HDHRChannel::GetHDHRDevice(void)
{
	return _hdhomerun_device;
}

QString HDHRChannel::TunerGet(const QString &name, bool report_error_return)
{
    return DeviceGet(QString("/tuner%1/%2").arg(_tuner).arg(name),
                     report_error_return);
}

QString HDHRChannel::TunerSet(const QString &name, const QString &value,
                              bool report_error_return)
{
    return DeviceSet(QString("/tuner%1/%2").arg(_tuner).arg(name), value,
                     report_error_return);
}

bool HDHRChannel::DeviceSetTarget(unsigned short localPort)
{
    if (localPort == 0)
    {
        return false;
    }

    unsigned long localIP = hdhomerun_device_get_local_machine_addr(
        _hdhomerun_device);
    if (localIP == 0)
    {
        return false;
    }

    QString configValue = QString("%1.%2.%3.%4:%5")
        .arg((localIP >> 24) & 0xFF).arg((localIP >> 16) & 0xFF)
        .arg((localIP >>  8) & 0xFF).arg((localIP >>  0) & 0xFF)
        .arg(localPort);

    if (hdhomerun_device_set_tuner_target(_hdhomerun_device, configValue) <= 0) 
    {
        return false;
    }

    return true;
}

bool HDHRChannel::DeviceClearTarget(void)
{
    return hdhomerun_device_set_tuner_target(_hdhomerun_device, "none") > 0;
}

bool HDHRChannel::SetChannelByString(const QString &channum)
{
    QString loc = LOC + QString("SetChannelByString(%1)").arg(channum);
    QString loc_err = loc + ", Error: ";
    VERBOSE(VB_CHANNEL, loc);
    
    if (!Open())
    {
        VERBOSE(VB_IMPORTANT, loc_err + "Channel object "
                "will not open, can not change channels.");

        return false;
    }

    QString inputName;
    if (!CheckChannel(channum, inputName))
    {
        VERBOSE(VB_IMPORTANT, loc_err +
                "CheckChannel failed.\n\t\t\tPlease verify the channel "
                "in the 'mythtv-setup' Channel Editor.");

        return false;
    }

    // If CheckChannel filled in the inputName then we need to
    // change inputs and return, since the act of changing
    // inputs will change the channel as well.
    if (!inputName.isEmpty())
        return SwitchToInput(inputName, channum);

    ClearDTVInfo();
    _ignore_filters = false;

    InputMap::const_iterator it = inputs.find(currentInputID);
    if (it == inputs.end())
        return false;

    uint mplexid_restriction;
    if (!IsInputAvailable(currentInputID, mplexid_restriction))
        return false;

    // Fetch tuning data from the database.
    QString tvformat, modulation, freqtable, freqid, si_std;
    int finetune;
    uint64_t frequency;
    int mpeg_prog_num;
    uint atsc_major, atsc_minor, mplexid, tsid, netid;

    if (!ChannelUtil::GetChannelData(
        (*it)->sourceid, channum,
        tvformat, modulation, freqtable, freqid,
        finetune, frequency,
        si_std, mpeg_prog_num, atsc_major, atsc_minor, tsid, netid,
        mplexid, commfree))
    {
        return false;
    }

    if (mplexid_restriction && (mplexid != mplexid_restriction))
        return false;

    // If the frequency is zeroed out, don't use it directly.
    bool ok = (frequency > 0);
    if (!ok)
    {
        frequency = (freqid.toInt(&ok) + finetune) * 1000;
        mplexid = 0;
    }
    bool isFrequency = ok && (frequency > 10000000);

    // Tune to proper frequency
    if ((*it)->externalChanger.isEmpty())
    {
        if (isFrequency)
        {
            if (!Tune(frequency, inputName, modulation, si_std))
                return false;
        }
        else
        {
            VERBOSE(VB_IMPORTANT, LOC_ERR +
                    "dtv_multiplex data is required for tuning");

            return false;
        }
    }
    else if (!ChangeExternalChannel(freqid))
        return false;

    // Set the current channum to the new channel's channum
    curchannelname = QDeepCopy<QString>(channum);

    // Set the major and minor channel for any additional multiplex tuning
    SetDTVInfo(atsc_major, atsc_minor, netid, tsid, mpeg_prog_num);

    // Set this as the future start channel for this source
    inputs[currentInputID]->startChanNum = QDeepCopy<QString>(curchannelname);

    // Turn on the HDHomeRun program filtering if it is supported
    // and we are tuning to an MPEG program number.
    if (mpeg_prog_num && (GetTuningMode() == "mpeg"))
    {
        QString pnum = QString::number(mpeg_prog_num);
        _ignore_filters = (hdhomerun_device_set_tuner_program(
                               _hdhomerun_device, pnum) > 0);
    }

    return true;
}

// documented in dtvchannel.h
bool HDHRChannel::TuneMultiplex(uint mplexid, QString inputname)
{
    VERBOSE(VB_CHANNEL, LOC + QString("TuneMultiplex(%1)").arg(mplexid));

    QString  modulation;
    QString  si_std;
    uint64_t frequency;
    uint     transportid;
    uint     dvb_networkid;

    if (!ChannelUtil::GetTuningParams(
            mplexid, modulation, frequency,
            transportid, dvb_networkid, si_std))
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "TuneMultiplex(): " +
                QString("Could not find tuning parameters for multiplex %1.")
                .arg(mplexid));

        return false;
    }

    if (!Tune(frequency, inputname, modulation, si_std))
        return false;

    return true;
}

bool HDHRChannel::Tune(const DTVMultiplex &tuning, QString inputname)
{
    return Tune(tuning.frequency, inputname,
                tuning.modulation.toString(), tuning.sistandard);
}

bool HDHRChannel::Tune(uint frequency, QString /*input*/,
                       QString modulation, QString si_std)
{
    QString chan = modulation + ':' + QString::number(frequency);

    VERBOSE(VB_CHANNEL, LOC + "Tune()ing to " + chan);

    if (hdhomerun_device_set_tuner_channel(_hdhomerun_device, chan) > 0)
    {
        SetSIStandard(si_std);
        return true;
    }


    // dtv_multiplex.modulation is from the DB. Could contain almost anything. 
    // As a fallback, use the HDHR device's automatic scanning: 
    chan = "auto:" + QString::number(frequency); 
 
    VERBOSE(VB_CHANNEL, LOC + "Failed. Now trying " + chan); 
 
    if (hdhomerun_device_set_tuner_channel(_hdhomerun_device, chan) > 0)
    { 
        SetSIStandard(si_std); 
        return true; 
    } 
 
 
    return false;
}

bool HDHRChannel::AddPID(uint pid, bool do_update)
{
    QMutexLocker locker(&_lock);

    vector<uint>::iterator it;
    it = lower_bound(_pids.begin(), _pids.end(), pid);
    if (it != _pids.end() && *it == pid)
    {
#ifdef DEBUG_PID_FILTERS
        VERBOSE(VB_CHANNEL, "AddPID(0x"<<hex<<pid<<dec<<") NOOP");
#endif // DEBUG_PID_FILTERS
        return true;
    }

    _pids.insert(it, pid);

#ifdef DEBUG_PID_FILTERS
    VERBOSE(VB_CHANNEL, "AddPID(0x"<<hex<<pid<<dec<<")");
#endif // DEBUG_PID_FILTERS

    if (do_update)
        return UpdateFilters();
    return true;
}

bool HDHRChannel::DelPID(uint pid, bool do_update)
{
    QMutexLocker locker(&_lock);

    vector<uint>::iterator it;
    it = lower_bound(_pids.begin(), _pids.end(), pid);
    if (it == _pids.end())
    {
#ifdef DEBUG_PID_FILTERS
        VERBOSE(VB_CHANNEL, "DelPID(0x"<<hex<<pid<<dec<<") NOOP");
#endif // DEBUG_PID_FILTERS

       return true;
    }

    if (*it == pid)
    {
#ifdef DEBUG_PID_FILTERS
        VERBOSE(VB_CHANNEL, "DelPID(0x"<<hex<<pid<<dec<<") -- found");
#endif // DEBUG_PID_FILTERS
        _pids.erase(it);
    }
    else
    {
#ifdef DEBUG_PID_FILTERS
        VERBOSE(VB_CHANNEL, "DelPID(0x"<<hex<<pid<<dec<<") -- failed");
#endif // DEBUG_PID_FILTERS
    }

    if (do_update)
        return UpdateFilters();
    return true;
}

bool HDHRChannel::DelAllPIDs(void)
{
    QMutexLocker locker(&_lock);

#ifdef DEBUG_PID_FILTERS
    VERBOSE(VB_CHANNEL, "DelAllPID()");
#endif // DEBUG_PID_FILTERS

    _pids.clear();

    return UpdateFilters();
}

QString filt_str(uint pid)
{
    uint pid0 = (pid / (16*16*16)) % 16;
    uint pid1 = (pid / (16*16))    % 16;
    uint pid2 = (pid / (16))        % 16;
    uint pid3 = pid % 16;
    return QString("0x%1%2%3%4")
        .arg(pid0,0,16).arg(pid1,0,16)
        .arg(pid2,0,16).arg(pid3,0,16);
}

bool HDHRChannel::UpdateFilters(void)
{
    QMutexLocker locker(&_lock);

    QString filter = "";

    vector<uint> range_min;
    vector<uint> range_max;

    if (_ignore_filters)
        return true;

    for (uint i = 0; i < _pids.size(); i++)
    {
        uint pid_min = _pids[i];
        uint pid_max  = pid_min;
        for (uint j = i + 1; j < _pids.size(); j++)
        {
            if (pid_max + 1 != _pids[j])
                break;
            pid_max++;
            i++;
        }
        range_min.push_back(pid_min);
        range_max.push_back(pid_max);
    }

    if (range_min.size() > 16)
    {
        range_min.resize(16);
        uint pid_max = range_max.back();
        range_max.resize(15);
        range_max.push_back(pid_max);
    }

    for (uint i = 0; i < range_min.size(); i++)
    {
        filter += filt_str(range_min[i]);
        if (range_min[i] != range_max[i])
            filter += QString("-%1").arg(filt_str(range_max[i]));
        filter += " ";
    }

    filter = filter.stripWhiteSpace();

    QString new_filter = TunerSet("filter", filter);

#ifdef DEBUG_PID_FILTERS
    QString msg = QString("Filter: '%1'").arg(filter);
    if (filter != new_filter)
        msg += QString("\n\t\t\t\t'%2'").arg(new_filter);

    VERBOSE(VB_CHANNEL, msg);
#endif // DEBUG_PID_FILTERS

    return filter == new_filter;
}
