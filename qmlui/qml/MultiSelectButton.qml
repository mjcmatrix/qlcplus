/*
  Q Light Controller Plus
  MultiSelectButton.qml

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

import QtQuick

import "."

// Toggle for the multiple selection mode of the lists, for touch screens
// where there is no Ctrl key. There are more than one of these in the UI,
// all showing the same state
IconButton
{
    imgSource: "qrc:/multiple.svg"
    tooltip: qsTr("Toggle multiple item selection") + " (" + qsTr("tap adds or removes items, press and hold selects a range") + ")"
    checkable: true
    checked: UISettings.multiSelectMode
    onToggled: UISettings.multiSelectMode = checked

    Connections
    {
        target: UISettings
        function onMultiSelectModeChanged() { checked = UISettings.multiSelectMode }
    }
}
