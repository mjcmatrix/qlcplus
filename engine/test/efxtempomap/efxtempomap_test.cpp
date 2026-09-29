/*
  Q Light Controller Plus - Unit test
  efxtempomap_test.cpp

  Copyright (c) Matt Carter (mjcmatrix)

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
#include <cmath>

#define protected public
#define private public
#include "efxtempomap_test.h"
#include "qlcfixturemode.h"
#include "qlcfixturedef.h"
#include "qlcfile.h"
#include "mastertimer.h"
#include "showfunction.h"
#include "showrunner.h"
#include "efxfixture.h"
#include "collection.h"
#include "tempomap.h"
#include "fixture.h"
#include "chaser.h"
#include "track.h"
#include "show.h"
#include "efx.h"
#include "doc.h"
#undef private
#undef protected

#include "../common/resource_paths.h"

#define TICK MasterTimer::tick()

/* Beats per tick at a tempo */
static double tickBeats(double bpm)
{
    return TICK * bpm / 60000.0;
}

/* The Show time the EFX has reached: its tempo map clock origin plus its
   elapsed time, which counts from the Show item start */
static double efxTime(EFX *efx)
{
    return efx->tempoMapClock()->origin + double(efx->elapsed());
}

/* Check that the fixture loop position matches the EFX beat count */
static bool fixtureInStep(EFX *efx, EFXFixture *ef)
{
    quint64 units = quint64(efx->m_beatPosition * 1000.0 + 1e-6);
    uint loop = efx->loopDuration();
    uint expected = units % loop;
    if (expected == 0 && units > 0)
        expected = loop;
    int diff = int(ef->m_elapsed) - int(expected);
    return qAbs(diff) <= 1 || qAbs(diff) >= int(loop) - 1;
}

void EFXTempoMap_Test::initTestCase()
{
    m_doc = new Doc(this);

    QDir dir(INTERNAL_FIXTUREDIR);
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QString("*%1").arg(KExtFixture));
    QVERIFY(m_doc->fixtureDefCache()->loadMap(dir));
}

void EFXTempoMap_Test::cleanupTestCase()
{
    delete m_doc;
}

void EFXTempoMap_Test::init()
{
    m_doc->masterTimer()->requestBpmNumber(120);
}

void EFXTempoMap_Test::cleanup()
{
    // stopAllFunctions() waits for the MasterTimer thread, not running here
    foreach (Function *function, m_doc->functions())
        function->stop(FunctionParent::master());
    tick(3);
    m_doc->clearContents();
}

EFX *EFXTempoMap_Test::createEFX(int fixtures, uint loopBeats)
{
    QLCFixtureDef* def = m_doc->fixtureDefCache()->fixtureDef("Martin", "MAC250+");
    QLCFixtureMode* mode = def->mode("Mode 4");

    EFX* e = new EFX(m_doc);
    m_doc->addFunction(e);

    for (int i = 0; i < fixtures; i++)
    {
        Fixture* fxi = new Fixture(m_doc);
        fxi->setFixtureDefinition(def, mode);
        fxi->setAddress(m_doc->fixtures().count() * 16);
        fxi->setUniverse(0);
        m_doc->addFixture(fxi);

        EFXFixture* ef = new EFXFixture(e);
        ef->setHead(GroupHead(fxi->id(), 0));
        e->addFixture(ef);
    }

    e->setTempoType(Function::Beats);
    e->setFadeInSpeed(0);
    e->setDuration(loopBeats * 1000);

    return e;
}

Show *EFXTempoMap_Test::createShow(const TempoMap &map)
{
    Show *show = new Show(m_doc);
    m_doc->addFunction(show);

    Track *track = new Track(Function::invalidId(), show);
    show->addTrack(track);

    // the items are in ms once the Show had sections, even when removed
    TempoMap anySection;
    anySection.addSection(TempoSection(0, 1000, 120));
    show->setTempoMap(anySection);
    show->setTempoMap(map);

    return show;
}

ShowFunction *EFXTempoMap_Test::addItem(Show *show, quint32 functionId, quint32 start, quint32 duration)
{
    ShowFunction *sf = show->tracks().first()->createShowFunction(functionId);
    sf->setStartTime(start);
    sf->setDuration(duration);
    return sf;
}

void EFXTempoMap_Test::tick(int count)
{
    for (int i = 0; i < count; i++)
        m_doc->masterTimer()->timerTick();
}

bool EFXTempoMap_Test::tickUntil(EFX *efx, double time)
{
    for (int i = 0; i < 2000; i++)
    {
        if (efx->isRunning() && efx->stopped() == false &&
            efx->tempoMapClock().isNull() == false && efxTime(efx) >= time)
            return true;
        tick();
    }
    qDebug() << "EFX running:" << efx->isRunning() << "clock:" << (efx->tempoMapClock().isNull() == false)
             << "time:" << (efx->tempoMapClock().isNull() ? -1.0 : efxTime(efx));
    return false;
}

bool EFXTempoMap_Test::tickUntilStopped(Show *show)
{
    for (int i = 0; i < 5000 && show->isRunning(); i++)
        tick();
    return show->isRunning() == false;
}

bool EFXTempoMap_Test::tickUntilStoppedEFX(EFX *efx)
{
    for (int i = 0; i < 5000 && efx->isRunning(); i++)
        tick();
    return efx->isRunning() == false;
}

quint32 EFXTempoMap_Test::showTime(Show *show) const
{
    return show->m_runner == NULL ? 0 : show->m_runner->m_elapsedTime;
}

void EFXTempoMap_Test::runChecked(Show *show, EFX *efx, quint32 itemStart,
                                  std::function<double(double)> expected, int maxTicks)
{
    show->start(m_doc->masterTimer(), FunctionParent::master());

    int efxTicks = 0;
    double previous = -1;
    bool wasRunning = false;

    for (int i = 0; i < maxTicks; i++)
    {
        tick();

        if (efx->isRunning() == false || efx->stopped())
        {
            if (wasRunning)
                break;
            continue;
        }
        wasRunning = true;
        efxTicks++;

        QVERIFY(efx->tempoMapClock().isNull() == false);
        QCOMPARE(efx->tempoMapClock()->origin, itemStart);

        // the EFX follows the Show time
        double time = efxTime(efx);
        QVERIFY(qAbs(time - showTime(show)) <= TICK);

        // and counts the beats of the tempo map
        double beats = efx->m_beatPosition;
        if (qAbs(beats - expected(time)) > 1e-6)
            qDebug() << "time" << time << "beats" << beats << "expected" << expected(time);
        QVERIFY(qAbs(beats - expected(time)) <= 1e-6);
        QVERIFY(beats > previous);
        previous = beats;

        foreach (EFXFixture *ef, efx->m_fixtures)
            QVERIFY(fixtureInStep(efx, ef));
    }

    QVERIFY(efxTicks > 0);
}

void EFXTempoMap_Test::itemOutsideSections()
{
    TempoMap map;
    map.addSection(TempoSection(10000, 10000, 90));
    Show *show = createShow(map);

    EFX *efx = createEFX(1, 4);
    addItem(show, efx->id(), 2000, 4000);

    // before the first section: the global BPM (120)
    runChecked(show, efx, 2000, [](double t) { return (t - 2000) / 500.0; }, 600);

    // stopped by the Show at the item end
    QVERIFY(efx->isRunning() == false);
}

void EFXTempoMap_Test::itemOverlapsSectionStart()
{
    TempoMap map;
    map.addSection(TempoSection(4000, 30000, 90));
    Show *show = createShow(map);

    EFX *efx = createEFX(1, 4);
    addItem(show, efx->id(), 2000, 6000);

    runChecked(show, efx, 2000, [](double t)
    {
        if (t <= 4000)
            return (t - 2000) / 500.0;
        return 4 + (t - 4000) * 90 / 60000.0;
    }, 600);
}

void EFXTempoMap_Test::itemOverlapsSectionEnd()
{
    TempoMap map;
    map.addSection(TempoSection(0, 4000, 90));
    Show *show = createShow(map);

    EFX *efx = createEFX(1, 4);
    addItem(show, efx->id(), 2000, 6000);

    // past the section end, the gap keeps the section tempo
    runChecked(show, efx, 2000, [](double t) { return (t - 2000) * 90 / 60000.0; }, 600);
}

void EFXTempoMap_Test::itemAcrossGap()
{
    TempoMap map;
    map.addSection(TempoSection(0, 3000, 120));
    map.addSection(TempoSection(5000, 30000, 60));
    Show *show = createShow(map);

    EFX *efx = createEFX(1, 3);
    addItem(show, efx->id(), 1000, 8000);

    runChecked(show, efx, 1000, [](double t)
    {
        if (t <= 5000)
            return (t - 1000) / 500.0;
        return 8 + (t - 5000) / 1000.0;
    }, 600);
}

void EFXTempoMap_Test::itemAcrossAdjacentSections()
{
    TempoMap map;
    map.addSection(TempoSection(0, 4000, 120));
    map.addSection(TempoSection(4000, 30000, 150));
    Show *show = createShow(map);

    EFX *efx = createEFX(2, 4);
    efx->setRunOrder(Function::PingPong);
    addItem(show, efx->id(), 1000, 7000);

    runChecked(show, efx, 1000, [](double t)
    {
        if (t <= 4000)
            return (t - 1000) / 500.0;
        return 6 + (t - 4000) / 400.0;
    }, 600);
}

void EFXTempoMap_Test::itemAcrossAdjacentOffGrid()
{
    TempoMap map;
    map.addSection(TempoSection(0, 4250, 120));
    map.addSection(TempoSection(4250, 30000, 60));
    Show *show = createShow(map);

    EFX *efx = createEFX(1, 4);
    addItem(show, efx->id(), 1000, 8000);

    // the second section starts halfway through a beat: the EFX catches up
    // the half beat over the first beat of the section
    runChecked(show, efx, 1000, [](double t)
    {
        if (t <= 4250)
            return (t - 1000) / 500.0;
        double p = (t - 4250) / 1000.0;
        return 6.5 + p + 0.5 * qMin(1.0, p);
    }, 600);
}

void EFXTempoMap_Test::itemOffGridStart()
{
    TempoMap map;
    map.addSection(TempoSection(0, 30000, 120));
    Show *show = createShow(map);

    EFX *efx = createEFX(1, 4);
    addItem(show, efx->id(), 1100, 4000);

    // the item starts 0.2 beats after a beat: eased onto the grid
    runChecked(show, efx, 1100, [](double t)
    {
        double p = (t - 1100) / 500.0;
        return p + 0.2 * qMin(1.0, p);
    }, 600);
}

void EFXTempoMap_Test::itemAllSectionsRemoved()
{
    Show *show = createShow(TempoMap());
    QVERIFY(show->itemsInMs());

    EFX *efx = createEFX(1, 4);
    addItem(show, efx->id(), 2000, 4000);

    // still on the (empty) tempo map, at the global BPM
    runChecked(show, efx, 2000, [](double t) { return (t - 2000) / 500.0; }, 600);
}

void EFXTempoMap_Test::fallbackBpmChange()
{
    Show *show = createShow(TempoMap());
    EFX *efx = createEFX(1, 4);
    addItem(show, efx->id(), 1000, 8000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickUntil(efx, 3000));
    QCOMPARE(efxTime(efx), 3000.0);
    QVERIFY(qAbs(efx->m_beatPosition - 4.0) < 1e-6);

    // the global BPM changes: the EFX goes on at the new tempo, no jump
    m_doc->masterTimer()->requestBpmNumber(60);
    for (int i = 1; i <= 100; i++)
    {
        tick();
        QVERIFY(qAbs(efx->m_beatPosition - (4 + i * tickBeats(60))) < 1e-6);
    }
}

void EFXTempoMap_Test::showStartedMidItem()
{
    TempoMap map;
    map.addSection(TempoSection(0, 4250, 120));
    map.addSection(TempoSection(4250, 30000, 60));
    Show *show = createShow(map);

    EFX *efx = createEFX(2, 3);
    efx->setRunOrder(Function::PingPong);
    addItem(show, efx->id(), 1000, 8000);

    auto expected = [](double t)
    {
        if (t <= 4250)
            return (t - 1000) / 500.0;
        double p = (t - 4250) / 1000.0;
        return 6.5 + p + 0.5 * qMin(1.0, p);
    };

    // started at several points in the item (in each section, on the
    // section boundary, in the easing beat): it is where it would be
    // if the Show had been played from the start
    for (quint32 start : { 1500u, 4250u, 4700u, 5000u, 8500u })
    {
        show->start(m_doc->masterTimer(), FunctionParent::master(), start);
        int checked = 0;
        for (int i = 0; i < 600 && show->stopped() == false; i++)
        {
            tick();
            if (efx->isRunning() == false || efx->stopped())
                continue;

            double t = efxTime(efx);
            QVERIFY(t >= start);
            QVERIFY(qAbs(t - showTime(show)) <= TICK);
            QVERIFY(qAbs(efx->m_beatPosition - expected(t)) < 1e-6);

            // PingPong: the direction changes on each loop of 3 beats
            quint64 units = quint64(efx->m_beatPosition * 1000.0 + 1e-6);
            uint loop = efx->loopDuration();
            foreach (EFXFixture *ef, efx->m_fixtures)
            {
                QVERIFY(fixtureInStep(efx, ef));
                if (units % loop > 2 && units % loop < loop - 2)
                    QCOMPARE(ef->m_runTimeDirection,
                             ((units / loop) % 2) ? Function::Backward : Function::Forward);
            }
            checked++;
        }
        QVERIFY(checked > 0);
        show->stop(FunctionParent::master());
        tick(2);
    }
}

void EFXTempoMap_Test::showStartedMidItemSerial()
{
    TempoMap map;
    map.addSection(TempoSection(0, 30000, 90));
    Show *show = createShow(map);

    // 3 fixtures in serial: each one starts 1 beat after the previous one
    EFX *efx = createEFX(3, 4);
    efx->setPropagationMode(EFX::Serial);
    addItem(show, efx->id(), 0, 20000);

    // started past the first loop (8.52 beats), early in a loop: all the
    // fixtures are running, even if their turn in this loop hasn't come
    show->start(m_doc->masterTimer(), FunctionParent::master(), 5680);
    tick(2);
    QVERIFY(efx->isRunning());
    QVERIFY(efx->m_fixtures.at(1)->m_elapsed < efx->m_fixtures.at(1)->timeOffset());
    for (int i = 0; i < 3; i++)
    {
        QVERIFY(efx->m_fixtures.at(i)->m_started);
        QVERIFY(fixtureInStep(efx, efx->m_fixtures.at(i)));
    }

    // started within the first beat: only the first one is
    show->stop(FunctionParent::master());
    tick(2);
    show->start(m_doc->masterTimer(), FunctionParent::master(), 300);
    tick(2);
    QVERIFY(efx->m_fixtures.at(0)->m_started);
    QVERIFY(efx->m_fixtures.at(1)->m_started == false);
    QVERIFY(efx->m_fixtures.at(2)->m_started == false);
}

void EFXTempoMap_Test::showStartedPastSingleShot()
{
    TempoMap map;
    map.addSection(TempoSection(0, 30000, 120));
    Show *show = createShow(map);

    // a single shot of 2 beats, in a 4 seconds item
    EFX *efx = createEFX(1, 2);
    efx->setRunOrder(Function::SingleShot);
    addItem(show, efx->id(), 1000, 4000);

    // from the start: done after 2 beats, before the item end
    show->start(m_doc->masterTimer(), FunctionParent::master());
    int i = 0;
    for (; i < 500 && efx->isRunning() == false; i++)
        tick();
    for (; i < 500 && efx->isRunning(); i++)
        tick();
    QVERIFY(efx->isRunning() == false);
    QVERIFY(showTime(show) > 2000 && showTime(show) < 2100);
    show->stop(FunctionParent::master());
    tick(2);

    // started after its single shot: done straight away
    show->start(m_doc->masterTimer(), FunctionParent::master(), 3500);
    tick(4);
    QVERIFY(efx->isRunning() == false);
    QVERIFY(show->isRunning());
}

void EFXTempoMap_Test::showPauseResume()
{
    TempoMap map;
    map.addSection(TempoSection(0, 4000, 120));
    map.addSection(TempoSection(4000, 30000, 150));
    Show *show = createShow(map);

    EFX *efx = createEFX(1, 4);
    addItem(show, efx->id(), 1000, 7000);

    auto expected = [](double t)
    {
        if (t <= 4000)
            return (t - 1000) / 500.0;
        return 6 + (t - 4000) / 400.0;
    };

    // pause across the section boundary, at several points
    show->start(m_doc->masterTimer(), FunctionParent::master());
    for (quint32 pauseAt : { 2000u, 3980u, 4000u, 6000u })
    {
        QVERIFY(tickUntil(efx, pauseAt));

        double beats = efx->m_beatPosition;
        quint32 elapsed = efx->elapsed();
        quint32 time = showTime(show);
        uint fixtureElapsed = efx->m_fixtures.at(0)->m_elapsed;

        // nothing moves while paused, beats or not
        show->setPause(true);
        QVERIFY(efx->isPaused());
        for (int i = 0; i < 40; i++)
        {
            m_doc->masterTimer()->m_beatRequested = (i % 10 == 0);
            tick();
        }
        QCOMPARE(efx->m_beatPosition, beats);
        QCOMPARE(efx->elapsed(), elapsed);
        QCOMPARE(showTime(show), time);
        QCOMPARE(efx->m_fixtures.at(0)->m_elapsed, fixtureElapsed);

        // resumed: carries on in step with the Show and the tempo map
        show->setPause(false);
        for (int i = 0; i < 30; i++)
        {
            tick();
            QVERIFY(qAbs(efxTime(efx) - showTime(show)) <= TICK);
            QVERIFY(qAbs(efx->m_beatPosition - expected(efxTime(efx))) < 1e-6);
            QVERIFY(fixtureInStep(efx, efx->m_fixtures.at(0)));
        }
    }
}

void EFXTempoMap_Test::showPauseStop()
{
    TempoMap map;
    map.addSection(TempoSection(0, 30000, 90));
    Show *show = createShow(map);

    EFX *efx = createEFX(1, 4);
    addItem(show, efx->id(), 2000, 6000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickUntil(efx, 4000));

    // paused, then stopped
    show->setPause(true);
    tick(10);
    show->stop(FunctionParent::master());
    tick(3);
    QVERIFY(show->isRunning() == false);
    QVERIFY(efx->isRunning() == false);
    QVERIFY(efx->isPaused() == false);
    QVERIFY(efx->tempoMapClock().isNull());
    QCOMPARE(efx->m_fixtures.at(0)->m_elapsed, uint(0));

    // started again: from the item start, not from where it was paused
    runChecked(show, efx, 2000, [](double t) { return (t - 2000) * 90 / 60000.0; }, 600);
}

void EFXTempoMap_Test::showStopRestart()
{
    TempoMap map;
    map.addSection(TempoSection(0, 4000, 120));
    map.addSection(TempoSection(4000, 30000, 150));
    Show *show = createShow(map);

    EFX *efx = createEFX(1, 4);
    addItem(show, efx->id(), 1000, 7000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickUntil(efx, 5000));
    show->stop(FunctionParent::master());
    tick(3);
    QVERIFY(efx->isRunning() == false);

    runChecked(show, efx, 1000, [](double t)
    {
        if (t <= 4000)
            return (t - 1000) / 500.0;
        return 6 + (t - 4000) / 400.0;
    }, 600);
}

/* How far the whole beats of $efx are from the beat grid of $show */
double EFXTempoMap_Test::gridOffset(EFX *efx, Show *show) const
{
    double offset = efx->m_beatPosition - show->tempoMap().gridPosition(showTime(show), 120);
    double error = offset - std::floor(offset + 0.5);
    if (qAbs(error) >= 0.001)
        qDebug() << "grid offset" << error << "beats" << efx->m_beatPosition << "time" << showTime(show)
                 << "grid" << show->tempoMap().gridPosition(showTime(show), 120) << "correction" << efx->m_beatCorrection
                 << "last grid" << efx->m_showGridPosition;
    return error;
}

/* Tick once and check that $efx moved on smoothly: forward, between half
   and 1.5 times the tempo, $bpm (or anything between $bpm and $otherBpm) */
bool EFXTempoMap_Test::tickSmooth(EFX *efx, double bpm, double otherBpm)
{
    double previous = efx->m_beatPosition;
    tick();
    double step = efx->m_beatPosition - previous;
    double low = 0.5 * tickBeats(qMin(bpm, otherBpm > 0 ? otherBpm : bpm));
    double high = 1.5 * tickBeats(qMax(bpm, otherBpm));
    if (step < low - 1e-9 || step > high + 1e-9)
    {
        qDebug() << "step" << step << "out of" << low << high;
        return false;
    }
    return true;
}

void EFXTempoMap_Test::efxOutsideShow()
{
    TempoMap map;
    map.addSection(TempoSection(0, 30000, 90));
    Show *show = createShow(map);

    EFX *showEFX = createEFX(1, 4);
    addItem(show, showEFX->id(), 0, 10000);

    // another EFX started from a VC widget while the Show runs its tempo
    // sections: the Show tempo takes over from the global BPM, and its
    // whole beats lock onto the Show grid beats within a beat
    EFX *vcEFX = createEFX(1, 4);
    vcEFX->start(m_doc->masterTimer(), FunctionParent(FunctionParent::ManualVCWidget, 0));
    tick(7);
    show->start(m_doc->masterTimer(), FunctionParent::master());
    tick();

    for (int i = 0; i < 200; i++)
    {
        // locked within a beat (33 ticks)
        QVERIFY(tickSmooth(vcEFX, 90, i > 35 ? 90 : 120));
        QVERIFY(vcEFX->tempoMapClock().isNull());
        QVERIFY(qAbs(showEFX->m_beatPosition - efxTime(showEFX) * 90 / 60000.0) < 1e-6);
        if (i > 35)
            QVERIFY(qAbs(gridOffset(vcEFX, show)) < 0.001);
    }

    // the Show stops: back to the global BPM, without a jump
    show->stop(FunctionParent::master());
    tick();
    QVERIFY(vcEFX->isRunning());
    QVERIFY(showEFX->isRunning() == false);
    for (int i = 0; i < 50; i++)
        QVERIFY(tickSmooth(vcEFX, 120));
}

void EFXTempoMap_Test::vcEFXSectionBecomesActive()
{
    // the Show plays at the global BPM up to its first section
    TempoMap map;
    map.addSection(TempoSection(3000, 30000, 75));
    Show *show = createShow(map);
    addItem(show, createEFX(1, 4)->id(), 0, 10000);

    EFX *vcEFX = createEFX(1, 4);
    show->start(m_doc->masterTimer(), FunctionParent::master());
    tick();
    vcEFX->start(m_doc->masterTimer(), FunctionParent(FunctionParent::ManualVCWidget, 0));

    // before the section: the global BPM
    while (showTime(show) < 3000)
        QVERIFY(tickSmooth(vcEFX, 120));

    // the section starts: 75 BPM from then, on its grid within a beat
    for (int i = 0; i < 200; i++)
    {
        // locked within a beat (40 ticks)
        bool locked = showTime(show) > 3000 + 800 + 20;
        QVERIFY(tickSmooth(vcEFX, 75, locked ? 75 : 120));
        if (locked)
            QVERIFY(qAbs(gridOffset(vcEFX, show)) < 0.001);
    }
}

void EFXTempoMap_Test::vcEFXShowPaused()
{
    TempoMap map;
    map.addSection(TempoSection(0, 30000, 100));
    Show *show = createShow(map);
    addItem(show, createEFX(1, 4)->id(), 0, 20000);

    EFX *vcEFX = createEFX(1, 4);
    show->start(m_doc->masterTimer(), FunctionParent::master());
    vcEFX->start(m_doc->masterTimer(), FunctionParent(FunctionParent::ManualVCWidget, 0));
    tick(100);
    QVERIFY(qAbs(gridOffset(vcEFX, show)) < 0.001);

    // the Show paused: the EFX goes on at the section tempo
    show->setPause(true);
    for (int i = 0; i < 100; i++)
    {
        double previous = vcEFX->m_beatPosition;
        tick();
        QVERIFY(qAbs(vcEFX->m_beatPosition - previous - tickBeats(100)) < 1e-6);
    }

    // resumed: it locks onto the grid again (now elsewhere) within a beat
    // (30 ticks), smoothly
    show->setPause(false);
    for (int i = 0; i < 100; i++)
    {
        QVERIFY(tickSmooth(vcEFX, 100));
        if (i > 31)
            QVERIFY(qAbs(gridOffset(vcEFX, show)) < 0.001);
    }

    // paused, then stopped: back to the global BPM
    show->setPause(true);
    tick(10);
    show->stop(FunctionParent::master());
    tick(2);
    for (int i = 0; i < 20; i++)
        QVERIFY(tickSmooth(vcEFX, 120));
}

void EFXTempoMap_Test::sharedEFXStartedOutsideFirst()
{
    TempoMap map;
    map.addSection(TempoSection(0, 30000, 90));
    Show *show = createShow(map);

    EFX *efx = createEFX(1, 4);
    addItem(show, efx->id(), 2000, 2000);
    addItem(show, createEFX(1, 4)->id(), 0, 6000);

    // started from a VC widget first, then reached by the Show: it stays
    // the VC widget EFX (no Show clock), following the Show tempo
    FunctionParent vc(FunctionParent::ManualVCWidget, 0);
    efx->start(m_doc->masterTimer(), vc);
    tick();
    show->start(m_doc->masterTimer(), FunctionParent::master());

    for (int i = 0; i < 250; i++)
    {
        QVERIFY(tickSmooth(efx, 90, i > 35 ? 90 : 120));
        QVERIFY(efx->isRunning());
        QVERIFY(efx->tempoMapClock().isNull());
        if (i > 35)
            QVERIFY(qAbs(gridOffset(efx, show)) < 0.001);
    }

    // past the Show end: the global BPM
    QVERIFY(tickUntilStopped(show));
    for (int i = 0; i < 20; i++)
        QVERIFY(tickSmooth(efx, 120, 90));
    efx->stop(vc);
    tick(2);
    QVERIFY(efx->isRunning() == false);
}

void EFXTempoMap_Test::sharedEFXStartedByShowFirst()
{
    TempoMap map;
    map.addSection(TempoSection(0, 30000, 90));
    Show *show = createShow(map);

    EFX *efx = createEFX(1, 4);
    addItem(show, efx->id(), 2000, 2000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickUntil(efx, 3000));

    // started from a VC widget too: it keeps the Show tempo map, and
    // carries on smoothly after the Show item ends
    FunctionParent vc(FunctionParent::ManualVCWidget, 0);
    efx->start(m_doc->masterTimer(), vc);
    double previous = efx->m_beatPosition;
    for (int i = 0; i < 200; i++)
    {
        tick();
        QVERIFY(efx->isRunning());
        QVERIFY(efx->tempoMapClock().isNull() == false);
        QVERIFY(qAbs(efx->m_beatPosition - previous - tickBeats(90)) < 1e-6);
        previous = efx->m_beatPosition;
    }
    QVERIFY(show->isRunning() == false);

    efx->stop(vc);
    tick(2);
    QVERIFY(efx->isRunning() == false);
}

void EFXTempoMap_Test::efxInCollection()
{
    TempoMap map;
    map.addSection(TempoSection(0, 30000, 150));
    Show *show = createShow(map);

    EFX *efx = createEFX(1, 4);
    Collection *collection = new Collection(m_doc);
    m_doc->addFunction(collection);
    collection->addFunction(efx->id());
    addItem(show, collection->id(), 1200, 4000);

    // the Collection hands its tempo map clock to the EFX
    runChecked(show, efx, 1200, [](double t) { return (t - 1200) / 400.0; }, 600);
}

void EFXTempoMap_Test::efxInChaser()
{
    TempoMap map;
    map.addSection(TempoSection(0, 30000, 90));
    Show *show = createShow(map);

    EFX *efx = createEFX(1, 2);
    Chaser *chaser = new Chaser(m_doc);
    chaser->setTempoType(Function::Beats);
    chaser->setDurationMode(Chaser::PerStep);
    chaser->addStep(ChaserStep(efx->id(), 0, 8000, 0));
    m_doc->addFunction(chaser);
    addItem(show, chaser->id(), 2000, 4000);

    // a Beats tempo Chaser step: the EFX follows the tempo map from the
    // step start, although the Chaser starts it in Time tempo
    runChecked(show, efx, 2000, [](double t) { return (t - 2000) * 90 / 60000.0; }, 600);
}

void EFXTempoMap_Test::timeEFXInTempoShow()
{
    TempoMap map;
    map.addSection(TempoSection(0, 30000, 90));
    Show *show = createShow(map);

    EFX *efx = createEFX(1, 4);
    efx->setTempoType(Function::Time);
    efx->setDuration(3000);
    addItem(show, efx->id(), 1000, 4000);

    // a Time tempo EFX runs in ms as before
    show->start(m_doc->masterTimer(), FunctionParent::master());
    for (int i = 0; i < 500 && efx->isRunning() == false; i++)
        tick();
    QVERIFY(efx->isRunning());
    QVERIFY(efx->tempoMapClock().isNull());
    uint e = efx->m_fixtures.at(0)->m_elapsed;
    for (int i = 1; i <= 100; i++)
    {
        tick();
        QCOMPARE(efx->m_fixtures.at(0)->m_elapsed, e + i * TICK);
    }
}

void EFXTempoMap_Test::fadeInOnTempoMap()
{
    TempoMap map;
    map.addSection(TempoSection(0, 30000, 60));
    Show *show = createShow(map);

    EFX *efx = createEFX(1, 4);
    efx->setFadeInSpeed(2000);
    efx->setDuration(6000);
    addItem(show, efx->id(), 1000, 8000);

    // a 2 beats fade in lasts 2 seconds at 60 BPM
    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickUntil(efx, 2000));
    QCOMPARE(efx->tempoElapsed(), quint32(1000));

    float x = 1, y = 1;
    efx->rotateAndScale(&x, &y);
    QVERIFY(qAbs(x - (127 + 127 * 0.5)) < 1.0);
}

/*********************************************************************
 * Live edits
 *********************************************************************/

void EFXTempoMap_Test::liveSectionEdits()
{
    TempoMap map;
    map.addSection(TempoSection(0, 4000, 120));
    map.addSection(TempoSection(4000, 30000, 90));
    Show *show = createShow(map);

    EFX *efx = createEFX(1, 4);
    addItem(show, efx->id(), 1000, 14000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickUntil(efx, 3000));

    // the section boundary moved from 4000 to 5000 ms: still 120 BPM up
    // to 5000, then 90 BPM on the new grid
    TempoMap moved;
    moved.addSection(TempoSection(0, 5000, 120));
    moved.addSection(TempoSection(5000, 30000, 90));
    show->setTempoMap(moved);
    while (showTime(show) < 4900)
        QVERIFY(tickSmooth(efx, 120));
    while (showTime(show) < 6000)
        QVERIFY(tickSmooth(efx, 90, 120));
    QCOMPARE(efx->tempoMapClock()->map.sections(), moved.sections());
    QCOMPARE(efx->tempoMapClock()->origin, quint32(1000));
    for (int i = 0; i < 20; i++)
    {
        QVERIFY(tickSmooth(efx, 90));
        QVERIFY(qAbs(gridOffset(efx, show)) < 0.02);
    }

    // the 90 BPM section deleted, off the 120 BPM grid: the gap goes on at
    // 120 BPM, locked onto the grid of the first section within a beat
    TempoMap deleted;
    deleted.addSection(TempoSection(0, 5000, 120));
    show->setTempoMap(deleted);
    quint32 editTime = showTime(show);
    while (showTime(show) < editTime + 700)
        QVERIFY(tickSmooth(efx, 120, 90));
    for (int i = 0; i < 20; i++)
    {
        QVERIFY(tickSmooth(efx, 120));
        QVERIFY(qAbs(gridOffset(efx, show)) < 0.02);
    }

    // a section added ahead of the cursor, off the grid: taken over when
    // reached, without a jump
    TempoMap added;
    added.addSection(TempoSection(0, 5000, 120));
    added.addSection(TempoSection(9130, 30000, 150));
    show->setTempoMap(added);
    while (showTime(show) < 9100)
        QVERIFY(tickSmooth(efx, 120));
    while (showTime(show) < 9130 + 420)
        QVERIFY(tickSmooth(efx, 150, 120));
    for (int i = 0; i < 20; i++)
    {
        QVERIFY(tickSmooth(efx, 150));
        QVERIFY(qAbs(gridOffset(efx, show)) < 0.02);
    }
}

void EFXTempoMap_Test::liveSectionEditsPaused()
{
    TempoMap map;
    map.addSection(TempoSection(0, 30000, 120));
    Show *show = createShow(map);

    EFX *efx = createEFX(1, 4);
    addItem(show, efx->id(), 1000, 14000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickUntil(efx, 3010));

    // edited while paused: nothing moves, the new tempo applies on resume
    show->setPause(true);
    tick();
    double beats = efx->m_beatPosition;
    TempoMap edited;
    edited.addSection(TempoSection(0, 3000, 120));
    edited.addSection(TempoSection(3000, 30000, 60));
    show->setTempoMap(edited);
    tick(30);
    QCOMPARE(efx->m_beatPosition, beats);

    show->setPause(false);
    for (int i = 0; i < 100; i++)
    {
        QVERIFY(tickSmooth(efx, 60));
        if (i > 55)
            QVERIFY(qAbs(gridOffset(efx, show)) < 0.02);
    }
}

void EFXTempoMap_Test::liveEFXItemAdded()
{
    TempoMap map;
    map.addSection(TempoSection(0, 30000, 120));
    Show *show = createShow(map);
    addItem(show, createEFX(1, 4)->id(), 0, 10000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    tick(100);

    // an EFX item added under the cursor starts from where the cursor is
    // in it, on the tempo map, as when a Show starts in the middle of it
    EFX *efx = createEFX(1, 4);
    addItem(show, efx->id(), 1000, 4000);
    for (int i = 0; i < 50; i++)
    {
        tick();
        QVERIFY(efx->isRunning());
        QCOMPARE(efx->tempoMapClock()->origin, quint32(1000));
        QVERIFY(qAbs(efx->m_beatPosition - (efxTime(efx) - 1000) / 500.0) < 1e-6);
        QVERIFY(fixtureInStep(efx, efx->m_fixtures.at(0)));
    }
}

void EFXTempoMap_Test::liveEFXItemAddedPaused()
{
    TempoMap map;
    map.addSection(TempoSection(0, 30000, 90));
    Show *show = createShow(map);
    addItem(show, createEFX(1, 4)->id(), 0, 10000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    tick(100);
    show->setPause(true);
    tick();

    // added while paused: starts on resume, in step with the Show
    EFX *efx = createEFX(1, 4);
    addItem(show, efx->id(), 0, 5000);
    tick(20);
    QVERIFY(efx->isRunning() == false);

    show->setPause(false);
    tick();
    QVERIFY(efx->isRunning());
    for (int i = 0; i < 50; i++)
    {
        tick();
        QVERIFY(qAbs(efxTime(efx) - showTime(show)) <= TICK);
        QVERIFY(qAbs(efx->m_beatPosition - efxTime(efx) * 90 / 60000.0) < 1e-6);
    }
}

void EFXTempoMap_Test::liveEFXItemMovedInPlace()
{
    TempoMap map;
    map.addSection(TempoSection(0, 30000, 120));
    Show *show = createShow(map);

    EFX *efx = createEFX(1, 4);
    ShowFunction *sf = addItem(show, efx->id(), 1000, 7000);
    addItem(show, createEFX(1, 4)->id(), 0, 12000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickUntil(efx, 3000));

    // moved, still under the cursor: the EFX goes on without a restart
    // or a jump (its clock keeps the start it had) and stops at its new end
    sf->setStartTime(1200);
    for (int i = 0; i < 100; i++)
    {
        QVERIFY(tickSmooth(efx, 120));
        QCOMPARE(efx->tempoMapClock()->origin, quint32(1000));
    }
    QVERIFY(tickUntilStoppedEFX(efx));
    QVERIFY(qAbs(double(showTime(show)) - 8200) <= 2 * TICK);
}

void EFXTempoMap_Test::liveEFXItemMovedAhead()
{
    TempoMap map;
    map.addSection(TempoSection(0, 30000, 120));
    Show *show = createShow(map);

    EFX *efx = createEFX(1, 4);
    ShowFunction *sf = addItem(show, efx->id(), 1000, 3000);
    addItem(show, createEFX(1, 4)->id(), 0, 12000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickUntil(efx, 2000));

    // moved past the cursor: it stops, and starts again from its new start
    sf->setStartTime(5000);
    tick(2);
    QVERIFY(efx->isRunning() == false);
    QVERIFY(tickUntil(efx, 5020));
    QCOMPARE(efx->tempoMapClock()->origin, quint32(5000));
    QVERIFY(qAbs(efx->m_beatPosition - (efxTime(efx) - 5000) / 500.0) < 1e-6);
}

void EFXTempoMap_Test::liveEFXItemResized()
{
    TempoMap map;
    map.addSection(TempoSection(0, 30000, 120));
    Show *show = createShow(map);

    EFX *efx = createEFX(1, 4);
    ShowFunction *sf = addItem(show, efx->id(), 1000, 3000);
    addItem(show, createEFX(1, 4)->id(), 0, 12000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickUntil(efx, 2000));

    // longer: runs on to its new end, without a jump
    sf->setDuration(6000);
    while (showTime(show) < 6900)
        QVERIFY(tickSmooth(efx, 120));
    QVERIFY(tickUntilStoppedEFX(efx));
    QVERIFY(qAbs(double(showTime(show)) - 7000) <= 2 * TICK);
}

void EFXTempoMap_Test::liveEFXDeleted()
{
    TempoMap map;
    map.addSection(TempoSection(0, 30000, 120));
    Show *show = createShow(map);

    EFX *efx = createEFX(1, 4);
    addItem(show, efx->id(), 1000, 3000);
    addItem(show, createEFX(1, 4)->id(), 0, 6000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickUntil(efx, 2000));

    // deleted as the Function Manager does: stopped, then deleted
    quint32 id = efx->id();
    efx->stop(FunctionParent::master());
    tick();
    QVERIFY(m_doc->deleteFunction(id));
    QVERIFY(tickUntilStopped(show));
}

void EFXTempoMap_Test::liveEFXDeletedPaused()
{
    TempoMap map;
    map.addSection(TempoSection(0, 30000, 120));
    Show *show = createShow(map);

    EFX *efx = createEFX(1, 4);
    addItem(show, efx->id(), 1000, 3000);
    addItem(show, createEFX(1, 4)->id(), 0, 6000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickUntil(efx, 2000));
    show->setPause(true);
    tick();

    quint32 id = efx->id();
    efx->stop(FunctionParent::master());
    tick();
    QVERIFY(m_doc->deleteFunction(id));
    tick(5);

    show->setPause(false);
    show->setPause(true);
    tick(2);
    show->setPause(false);
    QVERIFY(tickUntilStopped(show));
}

void EFXTempoMap_Test::liveEFXLoopEdited()
{
    TempoMap map;
    map.addSection(TempoSection(0, 30000, 120));
    Show *show = createShow(map);

    EFX *efx = createEFX(1, 4);
    addItem(show, efx->id(), 1000, 8000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickUntil(efx, 2330));

    // the loop edited while running: the beat count goes on, and the
    // fixture keeps its place in the loop
    EFXFixture *ef = efx->m_fixtures.at(0);
    float angle = ef->m_currentAngle;
    efx->setDuration(2500);
    QVERIFY(qAbs(ef->m_currentAngle - angle) < 0.01);
    for (int i = 0; i < 100; i++)
    {
        QVERIFY(tickSmooth(efx, 120));
        QVERIFY(ef->m_elapsed <= efx->loopDuration());
    }
}

void EFXTempoMap_Test::liveEFXTempoTypeSwitched()
{
    TempoMap map;
    map.addSection(TempoSection(0, 30000, 120));
    Show *show = createShow(map);

    EFX *efx = createEFX(1, 4);
    addItem(show, efx->id(), 1000, 8000);

    show->start(m_doc->masterTimer(), FunctionParent::master());
    QVERIFY(tickUntil(efx, 2000));

    // switched to Time while running: carries on in ms
    efx->setTempoType(Function::Time);
    QCOMPARE(efx->duration(), uint(2000));
    tick();
    EFXFixture *ef = efx->m_fixtures.at(0);
    for (int i = 0; i < 50; i++)
    {
        uint elapsed = ef->m_elapsed;
        tick();
        QVERIFY(ef->m_elapsed == elapsed + TICK || ef->m_elapsed == 0);
    }
}

QTEST_GUILESS_MAIN(EFXTempoMap_Test)
