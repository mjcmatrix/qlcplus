/*
  Q Light Controller Plus
  framepacer3d.h

  Copyright (c) Matt Carter

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

#ifndef FRAMEPACER3D_H
#define FRAMEPACER3D_H

#include <QObject>
#include <QPointer>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <Qt3DCore/QAbstractAspect>

class QQuickWindow;

Q_DECLARE_LOGGING_CATEGORY(lcFramePacer3D)

/** @addtogroup ui_vis
 * @{
 */

/**
 * A Qt 3D aspect that does nothing but notice when the scene changes.
 *
 * Every frontend node of the scene gets a lightweight backend node here,
 * so each node creation, destruction or property change bumps a counter.
 * FramePacer3D reads it to report how often the scene is redrawn, which
 * Scene3D does not expose. Registered by name ("qlcpacer") so it can be
 * listed in the Scene3D aspects.
 */
class ChangeAspect3D : public Qt3DCore::QAbstractAspect
{
    Q_OBJECT

public:
    explicit ChangeAspect3D(QObject *parent = nullptr);

    /** The number of scene changes seen so far, by any instance */
    static quint64 changeCount();

    static void countChange();
};

/**
 * Paces the changes made to the 3D scene so its redraws can't starve the UI.
 *
 * Scene3D renders as part of every Qt Quick frame, and Qt 3D makes the
 * GUI thread wait for the previous 3D frame before it starts the next one,
 * so when the GPU can't keep up the GUI thread spends nearly all its time
 * waiting and stops handling input.
 *
 * With RenderSettings.OnDemand, Qt 3D only redraws after the scene changed;
 * on the other frames it skips cheaply and Scene3D keeps showing the last
 * image. So the 3D view holds back its scene updates and applies them when
 * this class emits applyChanges().
 *
 * The budget is measured on the GUI thread: the time from the start of each
 * frame until the GUI thread gets back to its event loop is time it was
 * held up, and changes are only let through while that stays within the
 * share set by m_maxShare. A GPU that keeps up is let through on every frame; one
 * that doesn't redraws less often, rather than the whole UI slowing down
 * with it.
 *
 * Experimental: QLCPLUS_3D_PACER=0 lets the changes through on every frame
 * (the previous behaviour) and QLCPLUS_3D_PACER_SHARE sets m_maxShare.
 */
class FramePacer3D : public QObject
{
    Q_OBJECT

public:
    explicit FramePacer3D(QObject *parent = nullptr);

    /** Start pacing on the frames of @a window. A null window stops it */
    void attach(QQuickWindow *window);

    /** Whether the pacer is attached to a window: when it isn't,
     *  changes should be applied straight away */
    bool isActive() const;

signals:
    /** The time budget allows the 3D scene to change on this frame */
    void applyChanges();

private slots:
    void slotAfterAnimating();

private:
    QPointer<QQuickWindow> m_window;

    /** Time base for the frame intervals */
    QElapsedTimer m_clock;
    qint64 m_lastFrameTime;

    /** How long the last frame held up the GUI thread (ms) */
    double m_blockedMs;

    /** Time applyChanges() was last emitted (ms, m_clock) */
    qint64 m_lastApplyTime;

    /** GUI thread time owed (ms), see the class description */
    double m_debt;

    /** The share of the GUI thread time the 3D redraws may take */
    double m_maxShare;

    /** Let changes through on every frame (QLCPLUS_3D_PACER=0) */
    bool m_unpaced;

    /** Statistics for the debug log */
    quint64 m_statChangeCount;
    int m_statChangedFrames;
    int m_statApplies;
    int m_statFrames;
    double m_statBlockedMs;
    qint64 m_statStart;
};

/** @} */

#endif // FRAMEPACER3D_H
