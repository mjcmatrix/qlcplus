/*
  Q Light Controller
  showrunner.cpp

  Copyright (c) Massimo Callegari

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include <QMutex>
#include <QDebug>

#include "showrunner.h"
#include "function.h"
#include "track.h"
#include "show.h"
#include "doc.h"
#include "inputoutputmap.h"

#define TIMER_INTERVAL 50

/** Maximum time (in ms) a Show waits for an output to start */
#define OUTPUT_HOLD_TIMEOUT 2000

static bool compareShowFunctions(const ShowFunction *sf1, const ShowFunction *sf2)
{
    if (sf1->startTime() < sf2->startTime())
        return true;
    return false;
}

ShowRunner::ShowRunner(const Doc* doc, quint32 showID, quint32 startTime)
    : QObject(NULL)
    , m_doc(doc)
    , m_currentTimeFunctionIndex(0)
    , m_elapsedTime(startTime)
    , m_currentBeatFunctionIndex(0)
    , m_elapsedBeats(0)
    , beatSynced(false)
    , m_syncElapsedTime(0)
    , m_syncBeatsTime(0)
    , m_totalRunTime(0)
    , m_totalRunBeats(0)
    , m_tempoMapActive(false)
    , m_itemsDigest(0)
    , m_itemsSynced(false)
{
    Q_ASSERT(m_doc != NULL);
    Q_ASSERT(showID != Show::invalidId());

    m_show = qobject_cast<Show*>(m_doc->function(showID));
    if (m_show == NULL)
        return;

    m_tempoMapActive = m_show->itemsInMs();
    if (m_tempoMapActive)
        m_tempoMap = m_show->tempoMapSnapshot();

    /* startTime (e.g. coming from the cursor position) is always real
       milliseconds. If playback doesn't start from 0, m_elapsedBeats needs
       the equivalent estimate in "beats as ms" too, otherwise every Play
       would always start counting beats from 0 regardless of where
       playback actually begins - making beat-based Functions that should
       already be running (or long finished) at startTime behave as if
       playback had just begun. This is only a starting estimate: once the
       first real beat lands (see beatSynced in write()), m_elapsedBeats
       keeps advancing in lockstep with the actual beat clock from there */
    if (startTime > 0)
    {
        int bpm = m_doc->inputOutputMap()->bpmNumber();
        if (bpm > 0)
            m_elapsedBeats = Function::timeToBeats(startTime, 60000 / bpm);
    }

    foreach (Track *track, m_show->tracks())
    {
        // Initialize the intensity map
        if (track != NULL)
            m_intensityMap[track->id()] = 1.0;
    }

    syncItems();

    qDebug() << "ShowRunner created";
}

ShowRunner::~ShowRunner()
{
}

void ShowRunner::start()
{
    qDebug() << "ShowRunner started";
}

void ShowRunner::setPause(bool enable)
{
    // On resume, an audio device suspended during the pause has to wake up
    // again. Resume the Audio first and, if it isn't heard straight away,
    // hold the rest of the Show as when it is started (see write()).
    // The Show is still paused here, so write() doesn't run meanwhile.
    bool hold = false;

    if (enable == false)
    {
        for (int i = 0; i < m_runningQueue.count(); i++)
        {
            Function *f = m_doc->function(m_runningQueue.at(i).functionId);
            if (f != NULL && f->type() == Function::AudioType)
                f->setPause(false);
        }

        hold = isWaitingForOutput();
        if (hold && m_outputHold == false)
        {
            m_outputHold = true;
            m_outputHoldTime = 0;
        }
    }

    for (int i = 0; i < m_runningQueue.count(); i++)
    {
        Function *f = m_doc->function(m_runningQueue.at(i).functionId);
        if (f == NULL)
            continue;

        if (enable == false)
        {
            // already resumed above
            if (f->type() == Function::AudioType)
                continue;

            // Functions paused by an output hold stay paused until it's released
            if (m_holdPausedFunctions.contains(f->id()))
                continue;

            if (hold && f->type() != Function::VideoType && f->isPaused())
            {
                m_holdPausedFunctions.append(f->id());
                continue;
            }
        }

        f->setPause(enable);
    }
}

void ShowRunner::stop()
{
    m_elapsedTime = 0;
    m_elapsedBeats = 0;
    m_currentTimeFunctionIndex = 0;
    m_currentBeatFunctionIndex = 0;

    for (int i = 0; i < m_runningQueue.count(); i++)
    {
        Function *f = m_doc->function(m_runningQueue.at(i).functionId);
        if (f != NULL)
            f->stop(functionParent());
    }

    m_runningQueue.clear();
    m_startedItems.clear();
    m_itemsSynced = false;
    m_outputHold = false;
    m_preStartedItems.clear();
    m_holdPausedFunctions.clear();
    qDebug() << "ShowRunner stopped";
}

FunctionParent ShowRunner::functionParent() const
{
    return FunctionParent(FunctionParent::Function, m_show->id());
}

void ShowRunner::write(MasterTimer *timer)
{
    //qDebug() << Q_FUNC_INFO << "elapsed:" << m_elapsedTime << ", total:" << m_totalRunTime;

    // Phase -1. Follow the edits made to the Show since the last tick
    syncItems();

    // Phase 0. An Audio doesn't sound the moment it's started, so the Show
    // timeline (and every other item on it) waits for it to be heard,
    // otherwise the whole Show would run ahead of the music
    if (m_outputHold)
    {
        if (isWaitingForOutput() && m_outputHoldTime < OUTPUT_HOLD_TIMEOUT)
        {
            m_outputHoldTime += MasterTimer::tick();
            return;
        }
        releaseOutputHold();
    }
    else if (startOutputHold())
    {
        return;
    }

    // A Show can freely mix time-based and beat-based Functions on its
    // tracks (e.g. a beat-synced Chaser next to a Time-based Audio track),
    // regardless of the Show's own timeline display type. So beat tracking
    // must not depend on, nor gate, anything based on the Show's own
    // division: it only needs to run when this Show actually has beat-based
    // Functions to drive, and it must never block m_timeFunctions/m_elapsedTime
    // from progressing while waiting for the first beat to land.
    if (m_beatFunctions.isEmpty() == false && timer->isBeat())
    {
        if (beatSynced == false)
        {
            beatSynced = true;
            m_syncElapsedTime = m_elapsedTime;
            int syncBpm = timer->bpmNumber();
            m_syncBeatsTime = syncBpm > 0 ? Function::beatsToTime(m_elapsedBeats, 60000 / syncBpm) : 0;
            qDebug() << "Beat synced";
        }
        else
        {
            m_elapsedBeats += 1000;
        }
    }

    // Phase 1. Check if we need to stop some running Functions.
    // This is done before starting new ones, so that when an item ends
    // exactly where another item of the same Function begins, the Function
    // is stopped and started again (MasterTimer restarts it cleanly),
    // instead of the start being ignored and the stop winning.
    // It is done in reverse order for two reasons:
    // 1- m_runningQueue is not ordered by stop time
    // 2- to avoid messing up with indices when an entry is removed
    for (int i = m_runningQueue.count() - 1; i >= 0; i--)
    {
        RunningItem item = m_runningQueue.at(i);
        Function *func = m_doc->function(item.functionId);
        if (func == NULL)
        {
            m_runningQueue.removeAt(i);
            continue;
        }

        // if we passed the function stop time
        if (currentTime(func) >= item.stopTime)
        {
            // remove it from the running queue
            m_runningQueue.removeAt(i);
            // and stop it, unless another item still needs it running
            if (isQueued(item.functionId) == false)
                func->stop(functionParent());
        }
    }

    // Phase 2. Check all the Functions that need to be started
    // m_timeFunctions is ordered by startup time, so when we found an entry
    // with start time greater than m_elapsed, this phase is over
    bool startFunctionsDone = false;

    // check if there are time-based functions to start
    while (startFunctionsDone == false)
    {
        if (m_currentTimeFunctionIndex == m_timeFunctions.count())
            break;

        ShowFunction *sf = m_timeFunctions.at(m_currentTimeFunctionIndex);
        quint32 funcStartTime = sf->startTime();
        quint32 functionTimeOffset = 0;
        Function *f = m_doc->function(sf->functionID());
        if (f == nullptr || m_preStartedItems.remove(sf->id()))
        {
            m_currentTimeFunctionIndex++;
            continue;
        }

        // this should happen only when a Show is not started from 0
        if (m_elapsedTime > funcStartTime)
        {
            functionTimeOffset = m_elapsedTime - funcStartTime;
            funcStartTime = m_elapsedTime;
        }
        if (m_elapsedTime >= funcStartTime)
        {
            // The same Function can be used by more than one item, e.g. on
            // overlapping items on different tracks. A Function can run only
            // once, so if it is already running for another item it just
            // keeps running, until the last of its items ends
            if (isQueued(f->id()) == false)
            {
                requestTrackIntensity(sf, f);

                if (m_tempoMapActive && (f->tempoType() == Function::Beats || f->type() == Function::CollectionType))
                {
                    // run the Function beats on the tempo map, from the item start.
                    // A Collection hands the clock to its Beats tempo members
                    TempoMapClock clock(m_tempoMap, sf->startTime());
                    f->start(m_doc->masterTimer(), functionParent(), functionTimeOffset,
                             Function::defaultSpeed(), Function::defaultSpeed(), Function::defaultSpeed(),
                             Function::Original, &clock);
                }
                else
                {
                    f->start(m_doc->masterTimer(), functionParent(), functionTimeOffset);
                }
            }
            m_runningQueue.append({ sf->id(), f->id(), sf->startTime() + sf->duration(m_doc) });
            setItemStarted(sf);
            m_currentTimeFunctionIndex++;
        }
        else
            startFunctionsDone = true;
    }

    startFunctionsDone = false;

    // check if there are beat-based functions to start
    // (wait for the first real beat to land before considering any of
    // them, so m_elapsedBeats == 0 is not mistaken for "beat zero happened")
    while (startFunctionsDone == false && beatSynced)
    {
        if (m_currentBeatFunctionIndex == m_beatFunctions.count())
            break;

        ShowFunction *sf = m_beatFunctions.at(m_currentBeatFunctionIndex);
        quint32 funcStartTime = sf->startTime();
        quint32 functionTimeOffset = 0;
        Function *f = m_doc->function(sf->functionID());
        if (f == nullptr)
        {
            m_currentBeatFunctionIndex++;
            continue;
        }

        // this should happen only when a Show is not started from 0
        if (m_elapsedBeats > funcStartTime)
        {
            functionTimeOffset = m_elapsedBeats - funcStartTime;
            funcStartTime = m_elapsedBeats;
        }
        if (m_elapsedBeats >= funcStartTime)
        {
            // see the time-based items above
            if (isQueued(f->id()) == false)
            {
                requestTrackIntensity(sf, f);
                f->start(m_doc->masterTimer(), functionParent(), functionTimeOffset);
            }
            m_runningQueue.append({ sf->id(), f->id(), sf->startTime() + sf->duration(m_doc) });
            setItemStarted(sf);
            m_currentBeatFunctionIndex++;
        }
        else
            startFunctionsDone = true;
    }

    // Phase 3. Check if this is the end of the Show. A Show can mix
    // time-based and beat-based tracks, so it is only really over once
    // both the time-based and the beat-based timelines have completed.
    // While there are beat-based Functions but no beat has been detected
    // yet, the beat timeline hasn't even started, so it can't be "done".
    bool timeDone = m_elapsedTime >= m_totalRunTime;
    bool beatsDone = m_beatFunctions.isEmpty() ||
                      (beatSynced && m_elapsedBeats >= m_totalRunBeats);

    if (timeDone && beatsDone)
    {
        if (m_show != NULL)
            m_show->stop(functionParent());
        emit showFinished();
        return;
    }

    m_elapsedTime += MasterTimer::tick();

    // Report plain elapsed milliseconds: it advances smoothly on every
    // tick, and the UI scales it to a beat/bar position against the
    // current BPM (see ShowManager and TimeUtils.timeToBeatPosition()) so
    // it reacts immediately to live BPM changes.
    //
    // However, when the Show's own timeline is BPM based, m_elapsedTime is
    // real wall-clock time since the Show started, which includes however
    // long it took to wait for the first beat to sync (beat 0 is defined
    // by that sync moment, not by when the Show started) - reporting it
    // directly would make the cursor jump to an arbitrary, not
    // beat-aligned position the instant sync happens. Instead, report the
    // beat-zeroed resume position (m_syncBeatsTime) plus how much real
    // time has passed since the sync moment: this still advances smoothly
    // every tick (unlike reporting m_elapsedBeats directly, which only
    // moves in whole-beat jumps), while staying exactly beat-aligned at
    // the sync instant itself.
    if (m_show->timeDivisionType() == Show::Time)
    {
        emit timeChanged(m_elapsedTime);
    }
    else if (beatSynced)
    {
        emit timeChanged(m_syncBeatsTime + (m_elapsedTime - m_syncElapsedTime));
    }
}

/************************************************************************
 * Live edits
 ************************************************************************/

void ShowRunner::syncItems()
{
    if (m_show == NULL)
        return;

    // the first tempo section added while the Show runs puts its items in ms
    if (m_tempoMapActive == false && m_show->itemsInMs())
    {
        m_tempoMapActive = true;
        m_itemsSynced = false;
    }

    // tempo sections edited: the running Functions follow the new tempo map
    if (m_tempoMapActive)
    {
        TempoMap tempoMap = m_show->tempoMapSnapshot();
        if (tempoMap.sections() != m_tempoMap.sections())
        {
            m_tempoMap = tempoMap;
            for (int i = 0; i < m_runningQueue.count(); i++)
            {
                Function *f = m_doc->function(m_runningQueue.at(i).functionId);
                if (f != NULL)
                    f->updateTempoMap(m_tempoMap);
            }
        }
    }

    quint64 digest = itemsDigest();
    if (m_itemsSynced && digest == m_itemsDigest)
        return;

    m_itemsDigest = digest;
    m_itemsSynced = true;
    rebuildItems();
}

void ShowRunner::rebuildItems()
{
    QMap<quint32, ShowFunction *> items;
    QSet<quint32> mutedItems;

    // 1. the items of the Show, with a Function
    foreach (Track *track, m_show->tracks())
    {
        // some sanity checks
        if (track == NULL || track->id() == Track::invalidId())
            continue;

        if (m_intensityMap.contains(track->id()) == false)
            m_intensityMap[track->id()] = 1.0;

        foreach (ShowFunction *sf, track->showFunctions())
        {
            if (m_doc->function(sf->functionID()) == NULL)
                continue;

            items[sf->id()] = sf;
            if (track->isMute())
                mutedItems.insert(sf->id());
        }
    }

    // 2. the running items follow their item end. They stop if their item
    // or Function was deleted, if the item got another Function, or if the
    // item was moved past the cursor (it then plays again when reached)
    for (int i = m_runningQueue.count() - 1; i >= 0; i--)
    {
        RunningItem &item = m_runningQueue[i];
        ShowFunction *sf = items.value(item.itemId, NULL);

        if (sf != NULL && sf->functionID() == item.functionId &&
            sf->startTime() <= currentTime(m_doc->function(item.functionId)))
        {
            item.stopTime = sf->startTime() + sf->duration(m_doc);
            m_startedItems[sf->id()] = sf->startTime();
            continue;
        }

        quint32 functionId = item.functionId;
        m_runningQueue.removeAt(i);

        Function *f = m_doc->function(functionId);
        if (f != NULL && isQueued(functionId) == false)
            f->stop(functionParent());

        if (sf != NULL)
            m_startedItems.remove(sf->id());
    }

    // 3. the items to play: not started yet in this run (or moved since),
    // and not over
    QList<ShowFunction *> timeFunctions;
    QList<ShowFunction *> beatFunctions;

    m_totalRunTime = 0;
    m_totalRunBeats = 0;

    foreach (ShowFunction *sf, items)
    {
        // a muted track doesn't start its items
        if (mutedItems.contains(sf->id()))
            continue;

        Function *f = m_doc->function(sf->functionID());

        // with a tempo map, every item is positioned in ms. Otherwise
        // beat-based items are placed in "beats as ms", so they must be
        // compared with the beat position, not with real milliseconds
        bool inMs = f->tempoType() == Function::Time || m_tempoMapActive;
        quint32 endTime = sf->startTime() + sf->duration(m_doc);

        if (inMs)
            m_totalRunTime = qMax(m_totalRunTime, endTime);
        else
            m_totalRunBeats = qMax(m_totalRunBeats, endTime);

        if (m_startedItems.contains(sf->id()))
        {
            if (m_startedItems.value(sf->id()) == sf->startTime())
                continue;

            // moved since it played: it can play again
            m_startedItems.remove(sf->id());
        }

        if (endTime <= currentTime(f))
            continue;

        if (inMs)
            timeFunctions.append(sf);
        else
            beatFunctions.append(sf);
    }

    std::sort(timeFunctions.begin(), timeFunctions.end(), compareShowFunctions);
    std::sort(beatFunctions.begin(), beatFunctions.end(), compareShowFunctions);

    m_timeFunctions = timeFunctions;
    m_beatFunctions = beatFunctions;
    m_currentTimeFunctionIndex = 0;
    m_currentBeatFunctionIndex = 0;
    m_preStartedItems.clear();

    qDebug() << "[ShowRunner] items to play (time):" << m_timeFunctions.count()
             << "(beats):" << m_beatFunctions.count() << "running:" << m_runningQueue.count();
}

quint32 ShowRunner::currentTime(const Function *function) const
{
    if (function == NULL || function->tempoType() == Function::Time || m_tempoMapActive)
        return m_elapsedTime;

    return m_elapsedBeats;
}

quint64 ShowRunner::itemsDigest() const
{
    quint64 digest = 17;
    auto add = [&digest](quint64 value) { digest = digest * 1000003 + value; };

    foreach (Track *track, m_show->tracks())
    {
        if (track == NULL)
            continue;

        add(track->id());
        add(track->isMute());

        foreach (ShowFunction *sf, track->showFunctions())
        {
            Function *f = m_doc->function(sf->functionID());
            add(sf->id());
            add(sf->functionID());
            add(sf->startTime());
            add(sf->duration(m_doc));
            add(f == NULL ? 0 : 1 + f->tempoType());
        }
    }

    return digest;
}

void ShowRunner::setItemStarted(ShowFunction *sf)
{
    m_startedItems[sf->id()] = sf->startTime();
}

/************************************************************************
 * Output hold
 ************************************************************************/

bool ShowRunner::startOutputHold()
{
    bool started = false;

    for (int i = m_currentTimeFunctionIndex; i < m_timeFunctions.count(); i++)
    {
        ShowFunction *sf = m_timeFunctions.at(i);
        if (sf->startTime() > m_elapsedTime)
            break;

        Function *f = m_doc->function(sf->functionID());
        if (f == nullptr || f->type() != Function::AudioType ||
            m_preStartedItems.contains(sf->id()))
            continue;

        if (isQueued(f->id()) == false)
        {
            requestTrackIntensity(sf, f);
            f->start(m_doc->masterTimer(), functionParent(), m_elapsedTime - sf->startTime());
        }
        m_runningQueue.append({ sf->id(), f->id(), sf->startTime() + sf->duration(m_doc) });
        setItemStarted(sf);
        m_preStartedItems.insert(sf->id());
        started = true;
    }

    if (started == false)
        return false;

    // freeze what is already running (e.g. a Chaser spanning two songs), so
    // it doesn't run ahead either. Audio and Video keep their own clock.
    for (int i = 0; i < m_runningQueue.count(); i++)
    {
        Function *f = m_doc->function(m_runningQueue.at(i).functionId);
        if (f == NULL || f->type() == Function::AudioType || f->type() == Function::VideoType ||
            f->isRunning() == false || f->isPaused())
            continue;

        f->setPause(true);
        m_holdPausedFunctions.append(f->id());
    }

    m_outputHold = true;
    m_outputHoldTime = 0;

    return true;
}

void ShowRunner::releaseOutputHold()
{
    if (m_outputHoldTime >= OUTPUT_HOLD_TIMEOUT)
        qWarning() << "[ShowRunner] output didn't start within" << OUTPUT_HOLD_TIMEOUT << "ms. Continuing";
    else
        qDebug() << "[ShowRunner] output started after" << m_outputHoldTime << "ms";

    for (int i = 0; i < m_runningQueue.count(); i++)
    {
        Function *f = m_doc->function(m_runningQueue.at(i).functionId);
        if (f != NULL && m_holdPausedFunctions.contains(f->id()))
            f->setPause(false);
    }

    m_holdPausedFunctions.clear();
    m_outputHold = false;
}

bool ShowRunner::isWaitingForOutput() const
{
    for (int i = 0; i < m_runningQueue.count(); i++)
    {
        Function *f = m_doc->function(m_runningQueue.at(i).functionId);
        if (f != NULL && f->isWaitingForOutput())
            return true;
    }

    return false;
}

bool ShowRunner::isQueued(quint32 functionId) const
{
    for (int i = 0; i < m_runningQueue.count(); i++)
    {
        if (m_runningQueue.at(i).functionId == functionId)
            return true;
    }

    return false;
}

void ShowRunner::requestTrackIntensity(ShowFunction *sf, Function *f)
{
    foreach (Track *track, m_show->tracks())
    {
        if (track->showFunctions().contains(sf))
        {
            int intOverrideId = f->requestAttributeOverride(Function::Intensity, m_intensityMap[track->id()]);
            sf->setIntensityOverrideId(intOverrideId);
            break;
        }
    }
}

/************************************************************************
 * Intensity
 ************************************************************************/

void ShowRunner::adjustIntensity(qreal fraction, const Track *track)
{
    if (track == NULL)
        return;

    qDebug() << Q_FUNC_INFO << "Track ID: " << track->id() << ", val:" << fraction;
    m_intensityMap[track->id()] = fraction;

    foreach (ShowFunction *sf, track->showFunctions())
    {
        Function *f = m_doc->function(sf->functionID());
        if (f == NULL)
            continue;

        for (int i = 0; i < m_runningQueue.count(); i++)
        {
            if (m_runningQueue.at(i).functionId == f->id())
                f->adjustAttribute(fraction, sf->intensityOverrideId());
        }
    }
}

