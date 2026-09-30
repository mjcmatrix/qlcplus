/*
  Q Light Controller
  showrunner.h

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

#ifndef SHOWRUNNER_H
#define SHOWRUNNER_H

#include <QObject>
#include <QMutex>
#include <QMap>
#include <QSet>

#include <function.h>
#include "tempomap.h"

class ShowFunction;
class Function;
class Track;
class Show;
class Doc;

/** @addtogroup engine_functions Functions
 * @{
 */

class ShowRunner final : public QObject
{
    Q_OBJECT

public:
    ShowRunner(const Doc *doc, quint32 showID, quint32 startTime = 0);
    ~ShowRunner();

    /** Start the runner */
    void start();

    /** If running, pauses the runner and all the current running functions. */
    void setPause(bool enable);

    /** Stop the runner */
    void stop();

    void write(MasterTimer *timer);

    /** Publish the Show tempo on $timer when the Show sets the master
     *  tempo (see Show::masterTempo() and MasterTimer::setShowTempo()) */
    void publishTempo(MasterTimer *timer, bool paused);

private:
    /** The start order of this run among the Shows setting the master
     *  tempo, set when it first publishes its tempo */
    quint64 m_tempoOrder;

private:
    const Doc *m_doc;

    /** The reference of the show to play */
    Show* m_show;

    /** The list of time-based Functions the Show needs to play */
    QList <ShowFunction *> m_timeFunctions;

    /** Index of the item in m_timeFunctions to be considered for playback */
    int m_currentTimeFunctionIndex;

    /** Elapsed time since runner start. Used also to move the cursor in the track view */
    quint32 m_elapsedTime;

    /** The list of beat-based Functions the Show needs to play */
    QList <ShowFunction *> m_beatFunctions;

    /** Index of the item in m_beatFunctions to be considered for playback */
    int m_currentBeatFunctionIndex;

    /** Elapsed beats since runner start */
    quint32 m_elapsedBeats;

    /** Flag used to sinchronize playback to beats */
    bool beatSynced;

    /** m_elapsedTime at the moment beatSynced became true, and the ms
     *  equivalent of m_elapsedBeats at that same moment (i.e. the resumed
     *  beat position, if any). Used to derive a smoothly advancing, but
     *  beat-zeroed, cursor position for a BPM based Show (see write()) */
    quint32 m_syncElapsedTime;
    quint32 m_syncBeatsTime;

    /** Total time (in ms) the runner has to run, computed from m_timeFunctions */
    quint32 m_totalRunTime;

    /** Total time (in beats, expressed as ms, i.e. 1000 per beat) the
     *  runner has to run, computed from m_beatFunctions */
    quint32 m_totalRunBeats;

    /** A copy of the Show tempo map, when it drives the Show (see
     *  Show::itemsInMs()). All the items are then positioned in ms and
     *  Beats tempo Functions run on the tempo map beats, which follow the
     *  global BPM where there are no sections */
    bool m_tempoMapActive;
    TempoMap m_tempoMap;

    /** An item of the Show running its Function. Items and Functions are
     *  kept by ID, since both can be deleted while the Show runs */
    struct RunningItem
    {
        quint32 itemId;     // the ShowFunction ID
        quint32 functionId;
        quint32 stopTime;   // in ms, or in beats as ms for beat-based items
    };

    /** List of the items currently running their Function */
    QList<RunningItem> m_runningQueue;

private:
    FunctionParent functionParent() const;

    /************************************************************************
     * Live edits
     ************************************************************************/
private:
    /** Follow the edits made to the Show while it runs: items added, moved,
     *  resized or deleted, Functions deleted and tempo sections edited */
    void syncItems();

    /** Rebuild the lists of the items to play and update the running ones */
    void rebuildItems();

    /** Get a digest of the Show items, to notice when they are edited */
    quint64 itemsDigest() const;

    /** Get the Show position $function items are placed with: ms, or
     *  beats as ms for beat-based items without a tempo map */
    quint32 currentTime(const Function *function) const;

    /** Mark $sf as started in this run */
    void setItemStarted(ShowFunction *sf);

private:
    /** The items started in this run, with the start time they had then.
     *  An item moved to another start time can play again */
    QMap<quint32, quint32> m_startedItems;

    /** The digest of the Show items the lists were built from */
    quint64 m_itemsDigest;
    bool m_itemsSynced;

    /************************************************************************
     * Output hold
     ************************************************************************/
private:
    /** Start the Audio items due at m_elapsedTime ahead of everything else
     *  and hold the Show until they are actually heard (an audio device can
     *  take hundreds of ms to wake up). Returns true if a hold began */
    bool startOutputHold();

    /** Resume the Show after a hold */
    void releaseOutputHold();

    /** Returns true if any running Function is still waiting for its output */
    bool isWaitingForOutput() const;

    /** Apply the track intensity of $sf to its Function $f */
    void requestTrackIntensity(ShowFunction *sf, Function *f);

    /** Return true if the Function with ID $functionId is in the running
     *  queue for any item */
    bool isQueued(quint32 functionId) const;

private:
    /** True while the Show is held, waiting for an output to start */
    bool m_outputHold = false;

    /** How long (in ms) the current hold has lasted */
    quint32 m_outputHoldTime = 0;

    /** Items started by startOutputHold(), to be skipped by write() */
    QSet<quint32> m_preStartedItems;

    /** IDs of the Functions paused by startOutputHold(), resumed on release */
    QList<quint32> m_holdPausedFunctions;

signals:
    void timeChanged(quint32 time);
    void showFinished();

    /************************************************************************
     * Intensity
     ************************************************************************/
public:
    /**
     * Adjust the intensity of show track
     */
    void adjustIntensity(qreal fraction, const Track *track);

private:
    QMap<quint32, qreal> m_intensityMap;

};

/** @} */

#endif
