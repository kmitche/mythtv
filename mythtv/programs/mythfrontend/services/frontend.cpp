#include <QCoreApplication>
#include <QKeyEvent>

#include "mythcorecontext.h"
#include "keybindings.h"
#include "mythlogging.h"
#include "mythevent.h"
#include "mythuistatetracker.h"
#include "mythmainwindow.h"

#include "frontend.h"

#define LOC QString("Frontend API: ")

QStringList Frontend::gActionList = QStringList();
QHash<QString,QStringList> Frontend::gActionDescriptions = QHash<QString,QStringList>();

DTC::FrontendStatus* Frontend::GetStatus(void)
{
    DTC::FrontendStatus *status = new DTC::FrontendStatus();
    MythUIStateTracker::GetFreshState(status->State());
    return status;
}

bool Frontend::SendMessage(const QString &Message)
{
    if (Message.isEmpty())
        return false;

    qApp->postEvent(GetMythMainWindow(),
                    new MythEvent(MythEvent::MythUserMessage, Message));
    return true;
}

bool Frontend::SendAction(const QString &Action, const QString &File,
                          uint Width, uint Height)
{
    if (!IsValidAction(Action))
        return false;

    if (ACTION_HANDLEMEDIA == Action)
    {
        if (File.isEmpty())
        {
            LOG(VB_GENERAL, LOG_ERR, LOC + QString("No file specified."));
            return false;
        }

        MythEvent* me = new MythEvent(Action, QStringList(File));
        qApp->postEvent(GetMythMainWindow(), me);
        return true;
    }

    if (ACTION_SCREENSHOT == Action)
    {
        if (!Width || !Height)
        {
            LOG(VB_GENERAL, LOG_ERR, LOC + "Invalid screenshot parameters.");
            return false;
        }

        QStringList args;
        args << QString::number(Width) << QString::number(Height);
        MythEvent* me = new MythEvent(Action, args);
        qApp->postEvent(GetMythMainWindow(), me);
        return true;
    }

    QKeyEvent* ke = new QKeyEvent(QEvent::KeyPress, 0, Qt::NoModifier, Action);
    qApp->postEvent(GetMythMainWindow(), (QEvent*)ke);
    return true;
}

DTC::FrontendActionList* Frontend::GetActionList(void)
{
    DTC::FrontendActionList *list = new DTC::FrontendActionList();

    InitialiseActions();

    QHashIterator<QString,QStringList> contexts(gActionDescriptions);
    while (contexts.hasNext())
    {
        contexts.next();
        // TODO can we keep the context data with QMap<QString, QStringList>?
        QStringList actions = contexts.value();
        foreach (QString action, actions)
        {
            QStringList split = action.split(",");
            if (split.size() == 2)
                list->ActionList().insert(split[0], split[1]);
        }
    }
    return list;
}

bool Frontend::IsValidAction(const QString &Action)
{
    InitialiseActions();
    if (gActionList.contains(Action))
        return true;

    LOG(VB_GENERAL, LOG_ERR, LOC + QString("Action '%1'' is invalid.")
        .arg(Action));
    return false;
}

void Frontend::InitialiseActions(void)
{
    static bool initialised = false;
    if (initialised)
        return;

    initialised = true;
    KeyBindings *bindings = new KeyBindings(gCoreContext->GetHostName());
    if (bindings)
    {
        QStringList contexts = bindings->GetContexts();
        contexts.sort();
        foreach (QString context, contexts)
        {
            gActionDescriptions[context] = QStringList();
            QStringList ctx_actions = bindings->GetActions(context);
            ctx_actions.sort();
            gActionList += ctx_actions;
            foreach (QString actions, ctx_actions)
            {
                QString desc = actions + "," +
                               bindings->GetActionDescription(context, actions);
                gActionDescriptions[context].append(desc);
            }
        }
    }
    gActionList.removeDuplicates();
    gActionList.sort();

    foreach (QString actions, gActionList)
        LOG(VB_GENERAL, LOG_DEBUG, LOC + QString("Action: %1").arg(actions));
}
