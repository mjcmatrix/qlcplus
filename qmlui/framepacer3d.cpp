/*
  Q Light Controller Plus
  framepacer3d.cpp

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

#include <QHash>
#include <QQuickWindow>
#include <Qt3DCore/QNode>
#include <Qt3DCore/QBackendNode>

#include "framepacer3d.h"

Q_LOGGING_CATEGORY(lcFramePacer3D, "qlcplus.view3d.pacer", QtWarningMsg)

/*********************************************************************
 * ChangeAspect3D
 *********************************************************************/

static quint64 s_changeCount = 0;

namespace
{

class ChangeBackendNode : public Qt3DCore::QBackendNode
{
public:
    void syncFromFrontEnd(const Qt3DCore::QNode *frontEnd, bool firstTime) override
    {
        Qt3DCore::QBackendNode::syncFromFrontEnd(frontEnd, firstTime);
        ChangeAspect3D::countChange();
    }
};

class ChangeNodeMapper : public Qt3DCore::QBackendNodeMapper
{
public:
    ~ChangeNodeMapper() override
    {
        qDeleteAll(m_nodes);
    }

    Qt3DCore::QBackendNode *create(Qt3DCore::QNodeId id) const override
    {
        ChangeBackendNode *node = new ChangeBackendNode();
        m_nodes.insert(id, node);
        return node;
    }

    Qt3DCore::QBackendNode *get(Qt3DCore::QNodeId id) const override
    {
        return m_nodes.value(id, nullptr);
    }

    void destroy(Qt3DCore::QNodeId id) const override
    {
        delete m_nodes.take(id);
        ChangeAspect3D::countChange();
    }

private:
    mutable QHash<Qt3DCore::QNodeId, ChangeBackendNode *> m_nodes;
};

} // namespace

ChangeAspect3D::ChangeAspect3D(QObject *parent)
    : Qt3DCore::QAbstractAspect(parent)
{
    /* Mappers are looked up along the class hierarchy of each node, so
       registering QNode covers every node of the scene */
    registerBackendType<Qt3DCore::QNode>(QSharedPointer<ChangeNodeMapper>::create());
}

quint64 ChangeAspect3D::changeCount()
{
    return s_changeCount;
}

void ChangeAspect3D::countChange()
{
    // aspects sync their nodes on the GUI thread, in QAspectManager::processFrame
    s_changeCount++;
}

QT3D_REGISTER_ASPECT("qlcpacer", ChangeAspect3D)

/*********************************************************************
 * FramePacer3D
 *********************************************************************/

/* A frame interval this long means the window wasn't being drawn (hidden,
   minimized), not that a redraw held up the GUI thread, so it is capped */
#define MAX_COUNTED_MS          250.0
/* Bounds of the debt: some credit lets changes through right after an
   idle spell, and the cap stops a single stall holding them for long */
#define MIN_DEBT_MS             -20.0
#define MAX_DEBT_MS             250.0
/* However slow the frames, changes are still let through this often,
   so the 3D view slows down rather than freezes */
#define MAX_APPLY_INTERVAL_MS   200

FramePacer3D::FramePacer3D(QObject *parent)
    : QObject(parent)
    , m_lastFrameTime(-1)
    , m_blockedMs(0)
    , m_lastApplyTime(0)
    , m_debt(0)
    , m_maxShare(0.5)
    , m_unpaced(qEnvironmentVariable("QLCPLUS_3D_PACER") == QLatin1String("0"))
    , m_statChangeCount(0)
    , m_statChangedFrames(0)
    , m_statApplies(0)
    , m_statFrames(0)
    , m_statBlockedMs(0)
    , m_statStart(0)
{
    bool ok = false;
    double share = qEnvironmentVariable("QLCPLUS_3D_PACER_SHARE").toDouble(&ok);
    if (ok && share > 0)
        m_maxShare = qBound(0.05, share, 1.0);

    m_clock.start();
}

void FramePacer3D::attach(QQuickWindow *window)
{
    if (m_window)
        disconnect(m_window, &QQuickWindow::afterAnimating, this, &FramePacer3D::slotAfterAnimating);
    m_window = nullptr;

    if (window == nullptr)
        return;

    m_window = window;
    m_lastFrameTime = -1;
    m_blockedMs = 0;
    m_lastApplyTime = m_clock.elapsed();
    m_debt = 0;
    m_statChangeCount = ChangeAspect3D::changeCount();
    m_statChangedFrames = m_statApplies = m_statFrames = 0;
    m_statBlockedMs = 0;
    m_statStart = m_clock.elapsed();

    connect(window, &QQuickWindow::afterAnimating,
            this, &FramePacer3D::slotAfterAnimating, Qt::DirectConnection);

    if (m_unpaced)
        qCInfo(lcFramePacer3D) << "3D frame pacing disabled by QLCPLUS_3D_PACER=0";
    else
        qCInfo(lcFramePacer3D) << "3D frame pacing started, max GUI thread share" << m_maxShare;
}

bool FramePacer3D::isActive() const
{
    return m_window != nullptr;
}

void FramePacer3D::slotAfterAnimating()
{
    const qint64 now = m_clock.nsecsElapsed();
    if (m_lastFrameTime >= 0)
    {
        const double interval = qMin((now - m_lastFrameTime) / 1000000.0, MAX_COUNTED_MS);
        const double blocked = qMin(m_blockedMs, interval);

        /* Repay the time the last frame held up the GUI thread at the rate
           the budget allows. Over time this keeps it at no more than
           m_maxShare of the elapsed time */
        m_debt = qBound(MIN_DEBT_MS, m_debt + blocked - m_maxShare * interval, MAX_DEBT_MS);
        m_statFrames++;
        m_statBlockedMs += blocked;
    }
    m_lastFrameTime = now;
    m_blockedMs = 0;

    /* From here the GUI thread goes on to Qt 3D's processFrame(), which
       waits for the previous 3D frame, and to the Qt Quick sync, which waits
       for the render thread. This runs once it is back in its event loop,
       so the time it took is how long this frame held the GUI thread up */
    QMetaObject::invokeMethod(this, [this, now]()
    {
        m_blockedMs = (m_clock.nsecsElapsed() - now) / 1000000.0;
    }, Qt::QueuedConnection);

    const qint64 nowMs = m_clock.elapsed();
    if (m_unpaced || m_debt <= 0 || nowMs - m_lastApplyTime >= MAX_APPLY_INTERVAL_MS)
    {
        m_lastApplyTime = nowMs;
        m_statApplies++;
        emit applyChanges();
    }

    if (lcFramePacer3D().isDebugEnabled())
    {
        // a frame that synced scene changes is one Qt 3D redraws
        const quint64 changes = ChangeAspect3D::changeCount();
        if (changes != m_statChangeCount)
            m_statChangedFrames++;
        m_statChangeCount = changes;

        const qint64 elapsed = nowMs - m_statStart;
        if (elapsed >= 1000)
        {
            qCDebug(lcFramePacer3D).nospace() << "frames/s " << m_statFrames * 1000 / elapsed
                << " changed frames/s " << m_statChangedFrames * 1000 / elapsed
                << " applies/s " << m_statApplies * 1000 / elapsed
                << " GUI blocked " << int(m_statBlockedMs * 100 / elapsed) << "% debt " << m_debt << "ms";
            m_statChangedFrames = m_statApplies = m_statFrames = 0;
            m_statBlockedMs = 0;
            m_statStart = nowMs;
        }
    }
}
