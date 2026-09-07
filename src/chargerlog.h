// Charger lines of the kernel ring buffer, filtered once for both readers: the
// app reads the buffer itself where dmesg_restrict allows it, the root helper
// where it does not. Qualcomm names the steps (apsd, hvdcp), MediaTek prints
// policy-engine states (SNK_SEL_CAP, SNK_READY) plus a torrent of polling --
// hence the noise list ahead of the keep list.
#ifndef CHARGERLOG_H
#define CHARGERLOG_H

#include <QByteArray>
#include <QLatin1Char>
#include <QRegExp>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

inline QStringList chargerLogLines(const QByteArray &buf, int keepLines = 90)
{
    static const char *const noise[] = {
        "get_charger_status", "dump_registers", "mt6379_get_tchg",
        "charger_field_get", "alarm timer start", "snk_get_status",
        "good_crc", "tcpc-tcpc:alert", "bk_event_cb", "mt6379_transmit",
        "pe-evt:status", "pe-evt:tcp_event", "pe-evt:timer",
        "pd_tcp_notifier_call", "wakeup",
        // idle chatter while nothing is plugged in
        "vbat_mon", "get_charger_zcv", "enable_buck", "enable_hz",
        "enable_discharge", "charger_set_property", "pd_authentication",
        "cc_alert", "ps_change=", "alert_vendor_defined", "low_power_mode",
        "get pmic_node", "disable_charger",
        // BC1.2 plumbing; the line naming the charger type is kept
        "enable_bc12", "set_usbsw", "is_usb_rdy", "fl_bc12_dn_handler",
        "dpm:policy", "_pd_set_prop", "accdet_typec_callback",
        // direct charge dumps its working set every pass; keep decisions only
        "pe50_dump_charging_info", "pe50_check_ta_status", "pe50_check_vbatovp",
        "pe50_check_ta_ibusocp", "pe50_check_thermal_level", "pe50_select_vbat_cv",
        "pe50_set_dvchg_protection", "pe50_get_dvchg_ibusocp",
        "pe50_enable_ta_charging en =", "pe50_algo_init_with_ta_cv",
        "fgauged",
    };
    static const char *const keep[] = {
        "charger", "pd_", "usbpd", "typec", "type-c", "tcpc", "apsd",
        "hvdcp", "smblib", "real_charger", "pmic", "icl_settled", " pd ",
        "power_supply", "bc12", "pe50", "dvchg", "match_cap",
    };

    static const char *const attachMarker[] = {
        "snk_start", "typec-attach", "typec_attach_thread", "usbin-plugin",
        "apsd_done", "bc12_work_func",
    };

    // Retrying drivers write the same handful of lines forever. A line that
    // repeats one of the last dozen is counted there instead of pushing the
    // attach out of the window; differing wording stays its own line.
    const QRegExp prefix(QStringLiteral("^\\[\\s*\\d+\\.\\d+\\]\\s*\\[\\s*T?\\d+\\]\\s*"));
    const QRegExp stamp(QStringLiteral("<\\d+\\.\\d+>"));

    QStringList out, norm;
    QVector<int> reps;
    for (const QByteArray &raw : buf.split('\n')) {
        QByteArray l = raw;
        // strip the "<pri>" syslog prefix
        if (l.startsWith('<')) {
            const int gt = l.indexOf('>');
            if (gt > 0)
                l = l.mid(gt + 1);
        }
        if (l.trimmed().isEmpty())
            continue;
        const QByteArray low = l.toLower();
        bool skip = false;
        for (const char *n : noise) {
            if (low.contains(n)) { skip = true; break; }
        }
        if (skip)
            continue;
        bool wanted = false;
        for (const char *k : keep) {
            if (low.contains(k)) { wanted = true; break; }
        }
        if (!wanted)
            continue;

        const QString text = QString::fromUtf8(l);
        QString key = text;
        key.remove(prefix);
        key.remove(stamp);
        int hit = -1;
        for (int b = norm.size() - 1; b >= 0 && b >= norm.size() - 12; --b)
            if (norm.at(b) == key) { hit = b; break; }
        if (hit >= 0) {
            ++reps[hit];
            continue;
        }
        out << text;
        norm << key;
        reps << 1;
    }

    for (int i = 0; i < out.size(); ++i)
        if (reps.at(i) > 1)
            out[i] += QStringLiteral("  [x%1]").arg(reps.at(i));
    if (out.size() <= keepLines)
        return out;

    // The handshake happened at the plug, which a plain tail loses on a long
    // charge. Window starts at the last attach; the newest lines are appended
    // because the current state is the other half of the answer.
    int attach = -1;
    for (int i = out.size() - 1; i >= 0; --i) {
        const QString low = out.at(i).toLower();
        bool hit = false;
        for (const char *a : attachMarker) {
            if (low.contains(QLatin1String(a))) { hit = true; break; }
        }
        if (hit) { attach = i; break; }
    }
    // run-up: the policy engine starts before the line that names the attach
    if (attach > 0)
        attach = qMax(0, attach - 10);
    if (attach < 0 || out.size() - attach <= keepLines)
        return out.mid(out.size() - keepLines);

    const int head = keepLines * 2 / 3;
    return out.mid(attach, head) + out.mid(out.size() - (keepLines - head));
}

#endif // CHARGERLOG_H
