/*
  Q Light Controller Plus - Test Unit
  showrunner_test.cpp

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

#include <QtTest>
#define protected public
#define private public
#include "showrunner.h"
#include "mastertimer.h"
#include "inputoutputmap.h"
#include "show.h"
#include "chaserrunner.h"
#include "chaser.h"
#undef private
#undef protected
#include "showfunction.h"
#include "fixture.h"
#include "track.h"
#include "tempomap.h"
#include "collection.h"
#include "chaser.h"
#include "scene.h"
#include "doc.h"
#include "showrunner_test.h"

void ShowRunner_Test::initTestCase()
{
    m_doc = new Doc(this);

    m_fixture = new Fixture(m_doc);
    m_fixture->setAddress(0);
    m_fixture->setUniverse(0);
    m_fixture->setChannels(1);
    QVERIFY(m_doc->addFixture(m_fixture));

    m_show = new Show(m_doc);
    m_doc->addFunction(m_show);
    m_scene = new Scene(m_doc);
    m_doc->addFunction(m_scene);
    m_track = new Track(m_scene->id());
    ShowFunction *sf = new ShowFunction(m_show->getLatestShowFunctionId());
    sf->setFunctionID(m_scene->id());
    sf->setStartTime(0);
    sf->setDuration(1000);
    m_track->addShowFunction(sf);
    m_show->addTrack(m_track);
}

void ShowRunner_Test::cleanupTestCase()
{
    delete m_doc;
}

void ShowRunner_Test::initRunner()
{
    ShowRunner runner(m_doc, m_show->id());
    QCOMPARE(runner.m_timeFunctions.count(), 1);
    QCOMPARE(runner.m_totalRunTime, quint32(1000));
}

void ShowRunner_Test::intensity()
{
    ShowRunner runner(m_doc, m_show->id());
    runner.adjustIntensity(0.5, m_track);
    QCOMPARE(runner.m_intensityMap[m_track->id()], 0.5);
}

void ShowRunner_Test::stopRunner()
{
    ShowRunner runner(m_doc, m_show->id());
    runner.m_elapsedTime = 500;
    runner.m_runningQueue.append({ 0, m_scene->id(), 1000 });
    runner.stop();
    QCOMPARE(runner.m_elapsedTime, quint32(0));
    QCOMPARE(runner.m_runningQueue.count(), 0);
}
void ShowRunner_Test::tempoMapRunner()
{
    Show *show = new Show(m_doc);
    m_doc->addFunction(show);

    Chaser *chaser = new Chaser(m_doc);
    chaser->setTempoType(Function::Beats);
    m_doc->addFunction(chaser);

    Track *track = new Track(Function::invalidId(), show);
    show->addTrack(track);

    TempoMap map;
    map.addSection(TempoSection(0, 60000, 127.5, 4, "Song"));
    show->setTempoMap(map);

    // with a tempo map, a Beats tempo item is positioned in ms
    ShowFunction *sf = track->createShowFunction(chaser->id());
    sf->setStartTime(1000);
    sf->setDuration(3000);

    ShowRunner runner(m_doc, show->id());
    QCOMPARE(runner.m_tempoMapActive, true);
    QCOMPARE(runner.m_timeFunctions.count(), 1);
    QCOMPARE(runner.m_beatFunctions.count(), 0);
    QCOMPARE(runner.m_totalRunTime, quint32(4000));

    // the Chaser is started at 1000 ms with the tempo map, from the item start
    int i = 0;
    for (; i < 100 && runner.m_runningQueue.isEmpty(); i++)
        runner.write(m_doc->masterTimer());
    QCOMPARE(runner.m_elapsedTime, quint32(1020));
    QVERIFY(chaser->tempoMapClock().isNull() == false);
    QCOMPARE(chaser->tempoMapClock()->origin, quint32(1000));
    QCOMPARE(chaser->tempoMapClock()->map.count(), 1);

    // and stopped at 4000 ms, not after 4000 beats, which also ends the Show
    for (; i < 300 && runner.m_runningQueue.isEmpty() == false; i++)
        runner.write(m_doc->masterTimer());
    QCOMPARE(runner.m_elapsedTime, quint32(4000));

    // started in the middle of the item: the Chaser gets the same origin
    // and the offset into the item
    chaser->stop(FunctionParent::master());
    ShowRunner midRunner(m_doc, show->id(), 2500);
    midRunner.write(m_doc->masterTimer());
    QCOMPARE(midRunner.m_runningQueue.count(), 1);
    QCOMPARE(chaser->tempoMapClock()->origin, quint32(1000));
    QCOMPARE(chaser->elapsed(), quint32(1500));
    midRunner.stop();

    // with all the sections removed, the items stay in ms and the Chaser
    // keeps running on the tempo map, at the global BPM
    show->setTempoMap(TempoMap());
    ShowRunner noSectionsRunner(m_doc, show->id());
    QCOMPARE(noSectionsRunner.m_tempoMapActive, true);
    QCOMPARE(noSectionsRunner.m_totalRunTime, quint32(4000));
}

void ShowRunner_Test::tempoMapCollection()
{
    Show *show = new Show(m_doc);
    m_doc->addFunction(show);

    Chaser *chaser = new Chaser(m_doc);
    chaser->setTempoType(Function::Beats);
    m_doc->addFunction(chaser);

    Chaser *timeChaser = new Chaser(m_doc);
    m_doc->addFunction(timeChaser);

    Collection *inner = new Collection(m_doc);
    m_doc->addFunction(inner);
    inner->addFunction(chaser->id());

    Collection *outer = new Collection(m_doc);
    m_doc->addFunction(outer);
    outer->addFunction(inner->id());
    outer->addFunction(timeChaser->id());

    Track *track = new Track(Function::invalidId(), show);
    show->addTrack(track);

    TempoMap map;
    map.addSection(TempoSection(0, 60000, 96.5, 4, "Song"));
    show->setTempoMap(map);

    ShowFunction *sf = track->createShowFunction(outer->id());
    sf->setStartTime(1000);
    sf->setDuration(3000);

    // started in the middle of the item: the Collection gets the tempo map
    // clock although it is a Time tempo Function
    ShowRunner runner(m_doc, show->id(), 2500);
    runner.write(m_doc->masterTimer());
    QCOMPARE(runner.m_runningQueue.count(), 1);
    QVERIFY(outer->tempoMapClock().isNull() == false);
    QCOMPARE(outer->tempoMapClock()->origin, quint32(1000));
    QCOMPARE(outer->elapsed(), quint32(1500));

    // it hands the clock and its offset to its members, through a nested
    // Collection, down to the Beats tempo Chaser
    outer->preRun(m_doc->masterTimer());
    inner->preRun(m_doc->masterTimer());
    QVERIFY(chaser->tempoMapClock().isNull() == false);
    QCOMPARE(chaser->tempoMapClock()->origin, quint32(1000));
    QCOMPARE(chaser->tempoMapClock()->map.count(), 1);
    QCOMPARE(chaser->elapsed(), quint32(1500));
    runner.stop();
    outer->stop(FunctionParent::master());

    // a Collection started outside a Show with tempo sections starts its
    // members as before: no clock, from their start
    Collection *plain = new Collection(m_doc);
    m_doc->addFunction(plain);
    plain->addFunction(chaser->id());
    chaser->stop(FunctionParent(FunctionParent::Function, inner->id()));
    plain->start(m_doc->masterTimer(), FunctionParent::master(), 1500);
    plain->preRun(m_doc->masterTimer());
    QVERIFY(plain->tempoMapClock().isNull());
    QVERIFY(chaser->tempoMapClock().isNull());
    QCOMPARE(chaser->elapsed(), quint32(0));
}

Scene *ShowRunner_Test::createScene()
{
    Scene *scene = new Scene(m_doc);
    scene->setValue(m_fixture->id(), 0, 255);
    m_doc->addFunction(scene);
    return scene;
}

Show *ShowRunner_Test::createShow(quint32 fid1, quint32 start1, quint32 fid2, quint32 start2, bool sameTrack)
{
    Show *show = new Show(m_doc);
    m_doc->addFunction(show);

    Track *t1 = new Track(Function::invalidId(), show);
    show->addTrack(t1);
    Track *t2 = t1;
    if (sameTrack == false)
    {
        t2 = new Track(Function::invalidId(), show);
        show->addTrack(t2);
    }

    ShowFunction *sf1 = t1->createShowFunction(fid1);
    sf1->setStartTime(start1);
    sf1->setDuration(200);
    ShowFunction *sf2 = t2->createShowFunction(fid2);
    sf2->setStartTime(start2);
    sf2->setDuration(200);

    return show;
}

/* Run $show until $until ms and return the number of ticks in which
 * $scene was running, between $from and $to ms */
int ShowRunner_Test::runningTicks(Show *show, Scene *scene, quint32 from, quint32 to, quint32 until)
{
    // ShowRunner starts the Show items on the Doc's own MasterTimer
    MasterTimer *timer = m_doc->masterTimer();
    int ticks = 0;

    show->start(timer, FunctionParent::master());
    for (quint32 ms = 0; ms < until; ms += MasterTimer::tick())
    {
        timer->timerTick();
        if (ms >= from && ms < to && scene->isRunning())
            ticks++;
    }
    show->stop(FunctionParent::master());
    timer->timerTick();
    timer->timerTick();

    return ticks;
}

void ShowRunner_Test::sameFunctionConsecutiveItems()
{
    // one item ends exactly where the next one, of the same Function, begins:
    // the Function must keep running for the second item too
    Scene *scene = createScene();
    Show *show = createShow(scene->id(), 0, scene->id(), 200, true);
    quint32 tick = MasterTimer::tick();

    int expected = int(160 / tick);
    QCOMPARE(runningTicks(show, scene, 220, 380, 380), expected);
    QVERIFY(scene->isRunning() == false);
}

void ShowRunner_Test::sameFunctionOverlappingItems()
{
    // the same Function on two tracks, 0-200 and 100-300: it must keep
    // running until the last of its items ends
    Scene *scene = createScene();
    Show *show = createShow(scene->id(), 0, scene->id(), 100, false);
    quint32 tick = MasterTimer::tick();

    int expected = int(60 / tick);
    QCOMPARE(runningTicks(show, scene, 220, 280, 300), expected);
    QVERIFY(scene->isRunning() == false);
}

void ShowRunner_Test::differentFunctionsConsecutiveItems()
{
    // unchanged behaviour: the first Function stops, the second one runs
    Scene *scene1 = createScene();
    Scene *scene2 = createScene();
    Show *show = createShow(scene1->id(), 0, scene2->id(), 200, true);

    QCOMPARE(runningTicks(show, scene1, 240, 380, 380), 0);
    Show *show2 = createShow(scene1->id(), 0, scene2->id(), 200, true);
    QCOMPARE(runningTicks(show2, scene2, 220, 380, 380), int(160 / MasterTimer::tick()));
}

void ShowRunner_Test::beatItemsWhenStartingMidway()
{
    // at 30 BPM a beat lasts 2000ms, so starting at 10000ms means starting
    // at beat 5 (5000 in "beats as ms"). An item on beats 6-8 must still run,
    // one on beats 0-4 must not
    m_doc->inputOutputMap()->setBeatGeneratorType(InputOutputMap::Internal);
    m_doc->inputOutputMap()->setBpmNumber(30);

    Scene *early = createScene();
    early->setTempoType(Function::Beats);
    Scene *late = createScene();
    late->setTempoType(Function::Beats);

    Show *show = new Show(m_doc);
    m_doc->addFunction(show);
    Track *track = new Track(Function::invalidId(), show);
    show->addTrack(track);
    ShowFunction *sf1 = track->createShowFunction(early->id());
    sf1->setStartTime(0);
    sf1->setDuration(4000);
    ShowFunction *sf2 = track->createShowFunction(late->id());
    sf2->setStartTime(6000);
    sf2->setDuration(2000);

    ShowRunner runner(m_doc, show->id(), 10000);
    QCOMPARE(runner.m_elapsedBeats, quint32(5000));
    QCOMPARE(runner.m_beatFunctions.count(), 1);
    QCOMPARE(runner.m_beatFunctions.first()->functionID(), late->id());

    m_doc->inputOutputMap()->setBeatGeneratorType(InputOutputMap::Disabled);
}

/*********************************************************************
 * Live edits
 *********************************************************************/

Show *ShowRunner_Test::createLiveShow()
{
    Show *show = new Show(m_doc);
    m_doc->addFunction(show);
    show->addTrack(new Track(Function::invalidId(), show));
    return show;
}

ShowFunction *ShowRunner_Test::addLiveItem(Show *show, quint32 fid, quint32 start, quint32 duration, int track)
{
    while (show->tracks().count() <= track)
        show->addTrack(new Track(Function::invalidId(), show));

    ShowFunction *sf = show->tracks().at(track)->createShowFunction(fid);
    sf->setStartTime(start);
    sf->setDuration(duration);
    return sf;
}

quint32 ShowRunner_Test::showTime(Show *show) const
{
    return show->m_runner == NULL ? 0 : show->m_runner->m_elapsedTime;
}

/* Tick the Doc MasterTimer until $show is at $time ms */
bool ShowRunner_Test::tickTo(Show *show, quint32 time)
{
    for (int i = 0; i < 5000; i++)
    {
        if (show->isRunning() && show->m_runner != NULL && showTime(show) >= time)
            return true;
        m_doc->masterTimer()->timerTick();
    }
    return false;
}

void ShowRunner_Test::stopShow(Show *show)
{
    show->stop(FunctionParent::master());
    m_doc->masterTimer()->timerTick();
    m_doc->masterTimer()->timerTick();
}

void ShowRunner_Test::liveAddItemAhead()
{
    Scene *scene = createScene();
    Scene *added = createScene();
    Show *show = createLiveShow();
    addLiveItem(show, scene->id(), 0, 2000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickTo(show, 300));

    // added ahead of the cursor: starts when reached, stops at its end
    addLiveItem(show, added->id(), 600, 400);
    QVERIFY(tickTo(show, 580));
    QVERIFY(added->isRunning() == false);
    QVERIFY(tickTo(show, 640));
    QVERIFY(added->isRunning());
    QCOMPARE(added->elapsed(), quint32(0) + (showTime(show) - 600) - MasterTimer::tick() + MasterTimer::tick());
    QVERIFY(tickTo(show, 1040));
    QVERIFY(added->isRunning() == false);
    stopShow(show);
}

void ShowRunner_Test::liveAddItemUnderCursor()
{
    Scene *scene = createScene();
    Scene *added = createScene();
    Show *show = createLiveShow();
    addLiveItem(show, scene->id(), 0, 2000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickTo(show, 500));

    // added under the cursor: started straight away, from where the cursor
    // is in the item, as when a Show is started in the middle of an item
    quint32 now = showTime(show);
    addLiveItem(show, added->id(), 200, 1000);
    m_doc->masterTimer()->timerTick();
    QVERIFY(added->isRunning());
    QVERIFY(added->elapsed() >= now - 200);
    QVERIFY(added->elapsed() <= now - 200 + 2 * MasterTimer::tick());
    stopShow(show);
}

void ShowRunner_Test::liveAddTrack()
{
    Scene *scene = createScene();
    Scene *added = createScene();
    Show *show = createLiveShow();
    addLiveItem(show, scene->id(), 0, 2000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickTo(show, 200));

    // an item on a new track plays, at full track intensity
    ShowFunction *sf = addLiveItem(show, added->id(), 400, 400, 1);
    QVERIFY(tickTo(show, 440));
    QVERIFY(added->isRunning());
    QCOMPARE(show->m_runner->m_intensityMap.value(show->tracks().at(1)->id()), 1.0);
    QVERIFY(sf->intensityOverrideId() != Function::invalidAttributeId());
    QCOMPARE(added->getAttributeValue(Function::Intensity), 1.0);
    stopShow(show);
}

void ShowRunner_Test::liveMovePendingItem()
{
    Scene *scene = createScene();
    Scene *moved = createScene();
    Show *show = createLiveShow();
    addLiveItem(show, scene->id(), 0, 3000);
    ShowFunction *sf = addLiveItem(show, moved->id(), 500, 300);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickTo(show, 200));

    // moved later: starts at its new start, not the old one
    sf->setStartTime(1500);
    QVERIFY(tickTo(show, 600));
    QVERIFY(moved->isRunning() == false);
    QVERIFY(tickTo(show, 1540));
    QVERIFY(moved->isRunning());
    QVERIFY(tickTo(show, 1840));
    QVERIFY(moved->isRunning() == false);

    // moved earlier, before the cursor, after it played: doesn't play again
    sf->setStartTime(1900);
    QVERIFY(tickTo(show, 1880));
    sf->setStartTime(100);
    QVERIFY(tickTo(show, 2000));
    QVERIFY(moved->isRunning() == false);
    stopShow(show);
}

void ShowRunner_Test::liveMoveRunningItemInPlace()
{
    Scene *scene = createScene();
    Show *show = createLiveShow();
    ShowFunction *sf = addLiveItem(show, scene->id(), 200, 1000);
    addLiveItem(show, createScene()->id(), 0, 3000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickTo(show, 600));
    QVERIFY(scene->isRunning());
    quint32 elapsed = scene->elapsed();

    // moved, still under the cursor: it goes on without restarting, and
    // stops at its new end
    sf->setStartTime(100);
    m_doc->masterTimer()->timerTick();
    QVERIFY(scene->isRunning());
    QVERIFY(scene->elapsed() > elapsed);
    QVERIFY(tickTo(show, 1080));
    QVERIFY(scene->isRunning());
    QVERIFY(tickTo(show, 1140));
    QVERIFY(scene->isRunning() == false);
    stopShow(show);
}

void ShowRunner_Test::liveMoveRunningItemAhead()
{
    Scene *scene = createScene();
    Show *show = createLiveShow();
    ShowFunction *sf = addLiveItem(show, scene->id(), 200, 500);
    addLiveItem(show, createScene()->id(), 0, 3000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickTo(show, 400));
    QVERIFY(scene->isRunning());

    // moved past the cursor: it stops, and plays again from its start
    // when the cursor reaches it
    sf->setStartTime(1000);
    QVERIFY(tickTo(show, 460));
    QVERIFY(scene->isRunning() == false);
    QVERIFY(tickTo(show, 1020));
    QVERIFY(scene->isRunning());
    QVERIFY(scene->elapsed() <= 2 * MasterTimer::tick());
    QVERIFY(tickTo(show, 1520));
    QVERIFY(scene->isRunning() == false);
    stopShow(show);
}

void ShowRunner_Test::liveMoveFinishedItemAhead()
{
    Scene *scene = createScene();
    Show *show = createLiveShow();
    ShowFunction *sf = addLiveItem(show, scene->id(), 0, 300);
    addLiveItem(show, createScene()->id(), 0, 3000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickTo(show, 500));
    QVERIFY(scene->isRunning() == false);

    // an item that already played, moved ahead of the cursor, plays again
    sf->setStartTime(1000);
    QVERIFY(tickTo(show, 1040));
    QVERIFY(scene->isRunning());
    stopShow(show);
}

void ShowRunner_Test::liveResizeRunningItem()
{
    Scene *scene = createScene();
    Show *show = createLiveShow();
    ShowFunction *sf = addLiveItem(show, scene->id(), 0, 500);
    addLiveItem(show, createScene()->id(), 0, 3000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickTo(show, 300));

    // longer: stops at its new end
    sf->setDuration(1000);
    QVERIFY(tickTo(show, 700));
    QVERIFY(scene->isRunning());
    QVERIFY(tickTo(show, 1040));
    QVERIFY(scene->isRunning() == false);
    stopShow(show);

    // shorter than where the cursor is: stops straight away
    sf->setDuration(1000);
    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickTo(show, 600));
    QVERIFY(scene->isRunning());
    sf->setDuration(400);
    m_doc->masterTimer()->timerTick();
    m_doc->masterTimer()->timerTick();
    QVERIFY(scene->isRunning() == false);
    stopShow(show);
}

void ShowRunner_Test::liveExtendLastItem()
{
    Scene *scene = createScene();
    Show *show = createLiveShow();
    ShowFunction *sf = addLiveItem(show, scene->id(), 0, 500);

    // the Show ends at the new end of its last item
    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickTo(show, 300));
    sf->setDuration(1000);
    QVERIFY(tickTo(show, 900));
    QVERIFY(show->isRunning());
    for (int i = 0; i < 10; i++)
        m_doc->masterTimer()->timerTick();
    QVERIFY(show->isRunning() == false);

    // and earlier when it's shortened
    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickTo(show, 300));
    sf->setDuration(400);
    for (int i = 0; i < 10; i++)
        m_doc->masterTimer()->timerTick();
    QVERIFY(show->isRunning() == false);
    QVERIFY(scene->isRunning() == false);
}

void ShowRunner_Test::liveDeleteRunningItem()
{
    Scene *scene = createScene();
    Show *show = createLiveShow();
    ShowFunction *sf = addLiveItem(show, scene->id(), 0, 1000);
    addLiveItem(show, createScene()->id(), 0, 3000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickTo(show, 300));
    QVERIFY(scene->isRunning());

    // the item is deleted: its Function stops
    show->tracks().first()->removeShowFunction(sf, true);
    m_doc->masterTimer()->timerTick();
    m_doc->masterTimer()->timerTick();
    QVERIFY(scene->isRunning() == false);
    QVERIFY(tickTo(show, 1200));
    QVERIFY(show->isRunning());
    stopShow(show);
}

void ShowRunner_Test::liveDeletePendingItem()
{
    Scene *scene = createScene();
    Show *show = createLiveShow();
    ShowFunction *sf = addLiveItem(show, scene->id(), 800, 400);
    addLiveItem(show, createScene()->id(), 0, 3000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickTo(show, 300));

    // deleted before being reached: it never plays
    show->tracks().first()->removeShowFunction(sf, true);
    QVERIFY(tickTo(show, 1000));
    QVERIFY(scene->isRunning() == false);
    stopShow(show);
}

void ShowRunner_Test::liveDeleteTrack()
{
    Scene *scene = createScene();
    Show *show = createLiveShow();
    addLiveItem(show, createScene()->id(), 0, 3000);
    addLiveItem(show, scene->id(), 0, 1000, 1);
    addLiveItem(show, createScene()->id(), 1500, 500, 1);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickTo(show, 300));
    QVERIFY(scene->isRunning());

    // the whole track is deleted, with a running and a pending item
    show->removeTrack(show->tracks().at(1)->id());
    m_doc->masterTimer()->timerTick();
    m_doc->masterTimer()->timerTick();
    QVERIFY(scene->isRunning() == false);
    QVERIFY(tickTo(show, 1800));
    stopShow(show);
}

void ShowRunner_Test::liveDeleteSharedItem()
{
    // the same Function on two overlapping items: deleting one of them
    // leaves it running for the other one
    Scene *scene = createScene();
    Show *show = createLiveShow();
    ShowFunction *sf1 = addLiveItem(show, scene->id(), 0, 600);
    addLiveItem(show, scene->id(), 200, 800, 1);
    addLiveItem(show, createScene()->id(), 0, 3000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickTo(show, 400));
    show->tracks().first()->removeShowFunction(sf1, true);
    QVERIFY(tickTo(show, 800));
    QVERIFY(scene->isRunning());
    QVERIFY(tickTo(show, 1040));
    QVERIFY(scene->isRunning() == false);
    stopShow(show);
}

void ShowRunner_Test::liveChangeItemFunction()
{
    Scene *scene = createScene();
    Scene *other = createScene();
    Show *show = createLiveShow();
    ShowFunction *sf = addLiveItem(show, scene->id(), 0, 1000);
    addLiveItem(show, createScene()->id(), 0, 3000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickTo(show, 300));

    // the item gets another Function: the old one stops, the new one plays
    sf->setFunctionID(other->id());
    m_doc->masterTimer()->timerTick();
    m_doc->masterTimer()->timerTick();
    QVERIFY(scene->isRunning() == false);
    QVERIFY(other->isRunning());
    QVERIFY(tickTo(show, 1040));
    QVERIFY(other->isRunning() == false);
    stopShow(show);
}

void ShowRunner_Test::liveDeleteRunningFunction()
{
    Scene *scene = createScene();
    Scene *next = createScene();
    Show *show = createLiveShow();
    addLiveItem(show, scene->id(), 0, 1000);
    addLiveItem(show, next->id(), 1200, 300);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickTo(show, 300));
    QVERIFY(scene->isRunning());

    // deleted as the Function Manager does: stopped, then deleted
    quint32 id = scene->id();
    scene->stop(FunctionParent::master());
    m_doc->masterTimer()->timerTick();
    QVERIFY(m_doc->deleteFunction(id));

    // the Show goes on without it
    QVERIFY(tickTo(show, 1240));
    QVERIFY(next->isRunning());
    for (int i = 0; i < 30; i++)
        m_doc->masterTimer()->timerTick();
    QVERIFY(show->isRunning() == false);
}

void ShowRunner_Test::liveDeleteFunctionWhilePaused()
{
    Scene *scene = createScene();
    Show *show = createLiveShow();
    addLiveItem(show, scene->id(), 0, 1000);
    addLiveItem(show, createScene()->id(), 0, 2000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickTo(show, 300));

    // paused, the Function deleted, then resumed and stopped
    show->setPause(true);
    m_doc->masterTimer()->timerTick();
    quint32 id = scene->id();
    scene->stop(FunctionParent::master());
    m_doc->masterTimer()->timerTick();
    QVERIFY(m_doc->deleteFunction(id));
    m_doc->masterTimer()->timerTick();

    show->setPause(false);
    QVERIFY(tickTo(show, 600));
    show->setPause(true);
    show->setPause(false);
    stopShow(show);
    QVERIFY(show->isRunning() == false);
}

void ShowRunner_Test::liveEditsWhilePaused()
{
    Scene *moved = createScene();
    Scene *added = createScene();
    Show *show = createLiveShow();
    addLiveItem(show, createScene()->id(), 0, 3000);
    ShowFunction *sf = addLiveItem(show, moved->id(), 1500, 300);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickTo(show, 500));

    // paused: edits made meanwhile apply on resume
    show->setPause(true);
    m_doc->masterTimer()->timerTick();
    quint32 now = showTime(show);
    sf->setStartTime(700);
    addLiveItem(show, added->id(), 300, 1000);
    for (int i = 0; i < 20; i++)
        m_doc->masterTimer()->timerTick();
    QCOMPARE(showTime(show), now);
    QVERIFY(added->isRunning() == false);

    show->setPause(false);
    m_doc->masterTimer()->timerTick();
    QVERIFY(added->isRunning());
    QVERIFY(tickTo(show, 740));
    QVERIFY(moved->isRunning());
    stopShow(show);
}

void ShowRunner_Test::liveMuteTrack()
{
    Scene *running = createScene();
    Scene *pending = createScene();
    Show *show = createLiveShow();
    addLiveItem(show, createScene()->id(), 0, 3000);
    addLiveItem(show, running->id(), 0, 1000, 1);
    addLiveItem(show, pending->id(), 800, 400, 1);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickTo(show, 300));

    // a track muted while playing doesn't start its next items. The ones
    // running go on, as before
    show->tracks().at(1)->setMute(true);
    QVERIFY(tickTo(show, 900));
    QVERIFY(running->isRunning());
    QVERIFY(pending->isRunning() == false);
    stopShow(show);
}

void ShowRunner_Test::liveTempoMapEdit()
{
    Chaser *chaser = new Chaser(m_doc);
    chaser->setTempoType(Function::Beats);
    chaser->addStep(ChaserStep(createScene()->id(), 0, 4000, 0));
    m_doc->addFunction(chaser);

    Chaser *inner = new Chaser(m_doc);
    inner->setTempoType(Function::Beats);
    inner->addStep(ChaserStep(createScene()->id(), 0, 4000, 0));
    m_doc->addFunction(inner);
    Collection *collection = new Collection(m_doc);
    m_doc->addFunction(collection);
    collection->addFunction(inner->id());

    Show *show = createLiveShow();
    TempoMap map;
    map.addSection(TempoSection(0, 10000, 120));
    show->setTempoMap(map);
    addLiveItem(show, chaser->id(), 500, 3000);
    addLiveItem(show, collection->id(), 500, 3000, 1);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickTo(show, 1000));
    QVERIFY(chaser->tempoMapClock().isNull() == false);

    // the tempo sections are edited: the running Functions (and the ones
    // run by a Collection or a Chaser) follow the new tempo map, keeping the
    // Show time they started at
    TempoMap edited;
    edited.addSection(TempoSection(0, 1500, 120));
    edited.addSection(TempoSection(1500, 10000, 90));
    show->setTempoMap(edited);
    m_doc->masterTimer()->timerTick();

    QCOMPARE(show->m_runner->m_tempoMap.count(), 2);
    QCOMPARE(chaser->tempoMapClock()->map.sections(), edited.sections());
    QCOMPARE(chaser->tempoMapClock()->origin, quint32(500));
    QCOMPARE(chaser->m_runner->m_tempoMapClock->map.sections(), edited.sections());
    QCOMPARE(collection->tempoMapClock()->map.sections(), edited.sections());
    QCOMPARE(inner->tempoMapClock()->map.sections(), edited.sections());

    // the Chaser next step ends on the new tempo map: 4 beats from 2500
    // are 1 beat at 120 BPM and... all at 90 BPM once past 1500
    QVERIFY(tickTo(show, 3000));
    stopShow(show);
}

void ShowRunner_Test::liveFirstTempoSection()
{
    m_doc->inputOutputMap()->setBeatGeneratorType(InputOutputMap::Internal);
    m_doc->inputOutputMap()->setBpmNumber(120);

    // the first tempo section added while a Show with beat-based items runs
    Scene *scene = createScene();
    scene->setTempoType(Function::Beats);
    Show *show = createLiveShow();
    addLiveItem(show, scene->id(), 0, 4000);
    addLiveItem(show, createScene()->id(), 0, 5000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickTo(show, 300));

    TempoMap map;
    map.addSection(TempoSection(0, 10000, 90));
    show->setTempoMap(map);
    QVERIFY(show->itemsInMs());
    QVERIFY(tickTo(show, 600));
    QVERIFY(show->m_runner->m_tempoMapActive);
    QCOMPARE(show->m_runner->m_tempoMap.count(), 1);
    stopShow(show);

    m_doc->inputOutputMap()->setBeatGeneratorType(InputOutputMap::Disabled);
}

void ShowRunner_Test::tempoMapStepEndsWithItem()
{
    // a Beats tempo Chaser item, on the tempo grid but not on a tick,
    // ending exactly where a step ends: the Chaser is stopped with the item,
    // without starting its next step for a tick before
    Scene *s1 = createScene();
    Scene *s2 = createScene();
    Chaser *chaser = new Chaser(m_doc);
    chaser->setTempoType(Function::Beats);
    chaser->setRunOrder(Function::Loop);
    chaser->setDurationMode(Chaser::Common);
    chaser->setDuration(1000);
    m_doc->addFunction(chaser);
    chaser->addStep(s1->id());
    chaser->addStep(s2->id());

    Show *show = createLiveShow();
    TempoMap map;
    // the item ends 3 ms after a tick, the case where a step ending on the
    // tick nearest to its end time was ended a tick before the item
    map.addSection(TempoSection(3, 60000, 120, 4, "Song"));
    show->setTempoMap(map);
    addLiveItem(show, chaser->id(), 1003, 1000, 0);
    addLiveItem(show, createScene()->id(), 0, 4000, 1);

    int s1Starts = 0;
    bool wasRunning = false;
    show->start(m_doc->masterTimer(), FunctionParent::master());
    for (int i = 0; i < 150; i++)
    {
        m_doc->masterTimer()->timerTick();
        if (s1->isRunning() && wasRunning == false)
            s1Starts++;
        wasRunning = s1->isRunning();
    }
    QVERIFY(chaser->isRunning() == false);
    QCOMPARE(s1Starts, 1);
    stopShow(show);
}

QTEST_GUILESS_MAIN(ShowRunner_Test)
