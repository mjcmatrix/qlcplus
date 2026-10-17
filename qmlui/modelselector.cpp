/*
  Q Light Controller Plus
  modelselector.cpp

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

#include "modelselector.h"
#include "listmodel.h"

ModelSelector::ModelSelector(QObject *parent)
    : QObject(parent)
    , m_previousIndex(-1)
    , m_priorIndex(-1)
    , m_itemsCount(0)
{
}

ModelSelector::~ModelSelector()
{
}

void ModelSelector::selectSingleItem(int index, ListModel *model)
{
    if (model == nullptr || m_selectedIndices.contains(quint32(index)))
        return;

    QModelIndex idx = model->index(index, 0, QModelIndex());
    model->setDataWithRole(idx, "isSelected", true);
    m_selectedIndices.append(index);
    m_itemsCount++;

    emit itemSelectionChanged(index, true);
}

void ModelSelector::deselectSingleItem(int index, ListModel *model)
{
    if (model == nullptr || m_selectedIndices.removeAll(quint32(index)) == 0)
        return;

    QModelIndex idx = model->index(index, 0, QModelIndex());
    model->setDataWithRole(idx, "isSelected", false);
    m_itemsCount--;

    emit itemSelectionChanged(index, false);
}

void ModelSelector::selectItem(int index, ListModel *model, int keyModifiers)
{
    if (model == nullptr)
        return;

    //qDebug() << "select item with index:" << index;
    const int anchorIndex = m_previousIndex;
    const int priorIndex = m_priorIndex;

    if (keyModifiers == 0)
        resetSelection(model);

    // handle multirow selection: the range from the anchor replaces the
    // selection, unless Ctrl is also pressed. The anchor doesn't move
    if ((keyModifiers & Qt::ShiftModifier) && anchorIndex >= 0)
    {
        if (!(keyModifiers & Qt::ControlModifier))
            resetSelection(model);

        int startIndex = qMin(index, anchorIndex);
        int endIndex = qMax(index, anchorIndex);

        for (int i = startIndex; i <= endIndex; i++)
            selectSingleItem(i, model);

        m_previousIndex = anchorIndex;
    }
    else if ((keyModifiers & Qt::ControlModifier) && !(keyModifiers & Qt::ShiftModifier) && m_selectedIndices.contains(quint32(index)))
    {
        // Ctrl + click on a selected item deselects it
        deselectSingleItem(index, model);
        m_previousIndex = m_selectedIndices.isEmpty() ? -1 : int(m_selectedIndices.last());
    }
    else
    {
        // Ctrl + select a single item
        selectSingleItem(index, model);
        m_previousIndex = index;
        m_priorIndex = (anchorIndex != index) ? anchorIndex : priorIndex;
    }
    emit itemsCountChanged(m_itemsCount);
}

void ModelSelector::extendSelection(int index, ListModel *model, bool additive)
{
    if (model == nullptr)
        return;

    // a press selected $index already and made it the anchor: the range
    // starts from the item that was the anchor before that
    int anchorIndex = (index == m_previousIndex) ? m_priorIndex : m_previousIndex;
    if (anchorIndex < 0)
        return;

    if (!additive)
        resetSelection(model);

    for (int i = qMin(index, anchorIndex); i <= qMax(index, anchorIndex); i++)
        selectSingleItem(i, model);

    m_previousIndex = anchorIndex;
    m_priorIndex = -1;
    emit itemsCountChanged(m_itemsCount);
}

void ModelSelector::resetSelection(ListModel *model)
{
    for (quint32 &sidx : m_selectedIndices)
    {
        QModelIndex idx = model->index(int(sidx), 0, QModelIndex());
        model->setDataWithRole(idx, "isSelected", false);
        emit itemSelectionChanged(sidx, false);
    }

    m_selectedIndices.clear();
    m_itemsCount = 0;
    m_previousIndex = -1;
    m_priorIndex = -1;
    emit itemsCountChanged(m_itemsCount);
}

QVariantList ModelSelector::itemsList() const
{
    QVariantList list;
    for (const quint32 &sidx : m_selectedIndices)
        list.append(sidx);

    return list;
}

int ModelSelector::itemsCount() const
{
    return m_itemsCount;
}
