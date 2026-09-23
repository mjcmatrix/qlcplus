/*
  Q Light Controller Plus
  AddContainerMenu.qml

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

import QtQuick
import QtQuick.Controls

import org.qlcplus.classes 1.0
import "."

Popup
{
    id: menuRoot
    padding: 0

    signal entryClicked(int fType)

    background:
        Rectangle
        {
            color: UISettings.bgMedium
        }

    Column
    {
        id: addContainerMenuEntries

        ContextMenuEntry
        {
            imgSource: "qrc:/folder.svg"
            entryText: qsTr("New folder")
            onClicked:
            {
                menuRoot.requestFolder()
                menuRoot.close()
            }
        }
        ContextMenuEntry
        {
            imgSource: "qrc:/chaser.svg"
            entryText: qsTr("New Chaser")
            onClicked: entryClicked(QLCFunction.ChaserType)
        }
        ContextMenuEntry
        {
            imgSource: "qrc:/collection.svg"
            entryText: qsTr("New Collection")
            onClicked: entryClicked(QLCFunction.CollectionType)
        }
    }
}
