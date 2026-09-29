/*
  Q Light Controller Plus
  VCSliderChannelsPanel.qml

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
import QtQuick.Layouts
import QtQuick.Controls

import org.qlcplus.classes 1.0
import "."

Rectangle
{
    id: panelRoot
    objectName: "vcSliderChannelsPanel"
    anchors.fill: parent
    color: "transparent"

    /** A reference to the VCSlider being edited. Set by the side panel loader */
    property var modelProvider: null
    /** Not used here. Declared for compatibility with the side panel loader */
    property bool allowEditing: true

    /** Cache of the currently controlled channels, as "fixtureID_channelIndex"
      * strings. Rebuilt whenever the slider channel list changes, so that
      * every check box in this panel can bind to it */
    property var selectedKeys: []
    /** IDs of the fixtures expanded by the user in the browser. Kept here (rather
      * than in the delegates) so that expansion survives a browser model reload */
    property var expandedIds: []
    /** IDs of the fixtures expanded because the search filter matches one of
      * their channel names. Rebuilt on every browser reload, so they collapse
      * again when the search no longer matches them */
    property var searchExpandedIds: []
    /** The minimum height of the channel browser list */
    readonly property real browserMinHeight: UISettings.listItemHeight * 6

    onModelProviderChanged:
    {
        updateSelectedKeys()
        updateSearchExpanded()
    }

    Connections
    {
        target: modelProvider

        function onChannelsListChanged() { panelRoot.updateSelectedKeys() }
        function onBrowserFixturesChanged() { panelRoot.updateSearchExpanded() }
    }

    function channelKey(fxID, chIdx)
    {
        return fxID + "_" + chIdx
    }

    function updateSelectedKeys()
    {
        var keys = []

        if (modelProvider)
        {
            var list = modelProvider.channelsList
            for (var i = 0; i < list.length; i++)
                keys.push(channelKey(list[i].fxID, list[i].chIdx))
        }

        selectedKeys = keys
    }

    function isChannelSelected(fxID, chIdx)
    {
        return selectedKeys.indexOf(channelKey(fxID, chIdx)) !== -1
    }

    function fixtureSelectedCount(fxID)
    {
        var prefix = fxID + "_"
        var count = 0

        for (var i = 0; i < selectedKeys.length; i++)
            if (selectedKeys[i].indexOf(prefix) === 0)
                count++

        return count
    }

    /** Return how many of the given channel indices of a fixture are selected */
    function listedSelectedCount(fxID, chIndices)
    {
        var count = 0

        for (var i = 0; i < chIndices.length; i++)
            if (isChannelSelected(fxID, chIndices[i]))
                count++

        return count
    }

    function updateSearchExpanded()
    {
        var list = []

        if (modelProvider)
        {
            var fixtures = modelProvider.browserFixtures
            for (var i = 0; i < fixtures.length; i++)
                if (fixtures[i].channelNameMatch)
                    list.push(fixtures[i].fxID)
        }

        searchExpandedIds = list
    }

    function isFixtureExpanded(fxID)
    {
        return expandedIds.indexOf(fxID) !== -1 || searchExpandedIds.indexOf(fxID) !== -1
    }

    function toggleFixtureExpanded(fxID)
    {
        var list = expandedIds.slice()
        var idx = list.indexOf(fxID)

        if (isFixtureExpanded(fxID))
        {
            // collapse, whatever expanded it
            if (idx !== -1)
                list.splice(idx, 1)

            var searchList = searchExpandedIds.slice()
            var searchIdx = searchList.indexOf(fxID)
            if (searchIdx !== -1)
            {
                searchList.splice(searchIdx, 1)
                searchExpandedIds = searchList
            }
        }
        else
        {
            list.push(fxID)
        }

        expandedIds = list
    }

    function setAllFixturesExpanded(expand)
    {
        searchExpandedIds = []

        var list = []

        if (expand && modelProvider)
        {
            var fixtures = modelProvider.browserFixtures
            for (var i = 0; i < fixtures.length; i++)
                list.push(fixtures[i].fxID)
        }

        expandedIds = list
    }

    ColumnLayout
    {
        anchors.fill: parent
        spacing: 0

        /* *********************************************************************
         * Controlled channels
         ********************************************************************* */

        Rectangle
        {
            Layout.fillWidth: true
            implicitHeight: UISettings.iconSizeMedium
            color: UISettings.bgMedium

            RowLayout
            {
                anchors.fill: parent
                spacing: 2

                RobotoText
                {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    labelColor: UISettings.fgMain
                    label: qsTr("Controlled channels") +
                           " (" + (modelProvider ? modelProvider.channelsCount : 0) + ")"
                }

                IconButton
                {
                    Layout.preferredHeight: UISettings.iconSizeMedium
                    Layout.preferredWidth: UISettings.iconSizeMedium
                    faSource: FontAwesome.fa_trash
                    faColor: UISettings.fgMain
                    enabled: modelProvider ? modelProvider.channelsCount > 0 : false
                    tooltip: qsTr("Remove all the channels")
                    onClicked: if (modelProvider) modelProvider.clearChannelSelection()
                }
            }
        }

        Rectangle
        {
            Layout.fillWidth: true
            Layout.minimumHeight: UISettings.listItemHeight * 2
            // grow with the content, but never squeeze the channel browser
            // below browserMinHeight, so that it always remains usable
            Layout.preferredHeight: Math.min(selectedListView.contentHeight + 2,
                                             panelRoot.height * 0.5)
            color: UISettings.bgStrong
            border.width: 1
            border.color: UISettings.bgLight

            RobotoText
            {
                anchors.centerIn: parent
                visible: modelProvider ? modelProvider.channelsCount === 0 : true
                labelColor: UISettings.fgLight
                label: qsTr("No channel controlled yet")
            }

            ListView
            {
                id: selectedListView
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                model: modelProvider ? modelProvider.channelsList : null

                delegate:
                    Rectangle
                    {
                        width: selectedListView.width - (selScrollBar.visible ? selScrollBar.width : 0)
                        height: UISettings.listItemHeight
                        color: index % 2 ? "transparent" : UISettings.bgLight

                        required property int index
                        required property var modelData

                        RowLayout
                        {
                            anchors.fill: parent
                            spacing: 2

                            IconTextEntry
                            {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                iSrc: modelData.chIcon
                                tLabel: modelData.fxName + " - " + modelData.chName
                            }

                            RobotoText
                            {
                                Layout.fillHeight: true
                                labelColor: UISettings.fgLight
                                fontSize: UISettings.textSizeDefault * 0.8
                                label: modelData.universe + "." + modelData.dmxAddress
                            }

                            IconButton
                            {
                                Layout.preferredHeight: UISettings.listItemHeight
                                Layout.preferredWidth: UISettings.listItemHeight
                                faSource: FontAwesome.fa_minus
                                faColor: UISettings.fgMain
                                tooltip: qsTr("Remove this channel")
                                onClicked:
                                {
                                    if (modelProvider)
                                        modelProvider.setChannelSelection(modelData.fxID, modelData.chIdx, false)
                                }
                            }
                        }
                    }

                ScrollBar.vertical: CustomScrollBar { id: selScrollBar }
            }
        }

        /* *********************************************************************
         * Channel browser
         ********************************************************************* */

        Rectangle
        {
            Layout.fillWidth: true
            implicitHeight: UISettings.iconSizeMedium
            color: UISettings.bgMedium

            RowLayout
            {
                anchors.fill: parent
                spacing: 2

                RobotoText
                {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    labelColor: UISettings.fgMain
                    label: qsTr("Add channels")
                }

                IconButton
                {
                    Layout.preferredHeight: UISettings.iconSizeMedium
                    Layout.preferredWidth: UISettings.iconSizeMedium
                    faSource: FontAwesome.fa_angles_down
                    faColor: UISettings.fgMain
                    enabled: browserListView.count > 0
                    tooltip: qsTr("Expand all")
                    onClicked: panelRoot.setAllFixturesExpanded(true)
                }

                IconButton
                {
                    Layout.preferredHeight: UISettings.iconSizeMedium
                    Layout.preferredWidth: UISettings.iconSizeMedium
                    faSource: FontAwesome.fa_angles_up
                    faColor: UISettings.fgMain
                    enabled: panelRoot.expandedIds.length > 0 || panelRoot.searchExpandedIds.length > 0
                    tooltip: qsTr("Collapse all")
                    onClicked: panelRoot.setAllFixturesExpanded(false)
                }
            }
        }

        // search box
        Rectangle
        {
            Layout.fillWidth: true
            Layout.margins: 2
            implicitHeight: UISettings.iconSizeMedium
            color: UISettings.bgMedium
            radius: 3
            border.width: 2
            border.color: UISettings.borderColorDark

            RowLayout
            {
                anchors.fill: parent
                spacing: 2

                Text
                {
                    Layout.fillHeight: true
                    Layout.preferredWidth: UISettings.iconSizeMedium
                    color: UISettings.fgLight
                    font.family: UISettings.fontAwesomeFontName
                    font.pixelSize: UISettings.textSizeDefault
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                    text: FontAwesome.fa_magnifying_glass
                }

                TextInput
                {
                    id: searchInput
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: UISettings.fgMain
                    text: modelProvider ? modelProvider.searchFilter : ""
                    font.family: UISettings.robotoFontName
                    font.pixelSize: UISettings.textSizeDefault
                    selectionColor: UISettings.highlightPressed
                    selectByMouse: true
                    verticalAlignment: TextInput.AlignVCenter

                    onTextEdited: if (modelProvider) modelProvider.searchFilter = text

                    RobotoText
                    {
                        anchors.fill: parent
                        visible: searchInput.text.length === 0
                        labelColor: UISettings.fgLight
                        label: qsTr("Filter fixtures and channels")
                    }
                }

                IconButton
                {
                    Layout.preferredHeight: UISettings.iconSizeMedium
                    Layout.preferredWidth: UISettings.iconSizeMedium
                    visible: searchInput.text.length > 0
                    faSource: FontAwesome.fa_xmark
                    faColor: UISettings.fgMain
                    tooltip: qsTr("Clear the filter")
                    onClicked:
                    {
                        searchInput.text = ""
                        if (modelProvider)
                            modelProvider.searchFilter = ""
                    }
                }
            }
        }

        // channel type filter
        RowLayout
        {
            Layout.fillWidth: true
            Layout.fillHeight: false
            Layout.preferredHeight: UISettings.iconSizeMedium
            Layout.leftMargin: 2
            Layout.rightMargin: 2
            spacing: 2

            Repeater
            {
                model: [
                    { mLabel: qsTr("All"), mValue: VCSlider.AllChannelTypes },
                    { mLabel: qsTr("Dimmer"), mValue: VCSlider.DimmerChannelTypes },
                    { mLabel: qsTr("Colour"), mValue: VCSlider.ColourChannelTypes },
                    { mLabel: qsTr("Position"), mValue: VCSlider.PositionChannelTypes },
                    { mLabel: qsTr("Beam"), mValue: VCSlider.BeamChannelTypes }
                ]

                delegate:
                    GenericButton
                    {
                        required property var modelData

                        Layout.fillWidth: true
                        Layout.preferredHeight: UISettings.iconSizeMedium
                        fontSize: UISettings.textSizeDefault * 0.9
                        label: modelData.mLabel
                        bgColor: modelProvider && modelProvider.channelTypeFilter === modelData.mValue ?
                                     UISettings.highlight : UISettings.bgControl
                        onClicked: if (modelProvider) modelProvider.channelTypeFilter = modelData.mValue
                    }
            }
        }

        RowLayout
        {
            Layout.fillWidth: true
            Layout.fillHeight: false
            Layout.preferredHeight: UISettings.iconSizeMedium
            Layout.margins: 2
            spacing: 2

            CustomCheckBox
            {
                implicitWidth: UISettings.listItemHeight
                implicitHeight: implicitWidth
                checked: modelProvider ? modelProvider.applySameType : false
                onToggled: if (modelProvider) modelProvider.applySameType = checked
            }

            RobotoText
            {
                Layout.fillWidth: true
                Layout.fillHeight: true
                fontSize: UISettings.textSizeDefault * 0.9
                label: qsTr("Apply to fixtures of the same type")
            }

            GenericButton
            {
                Layout.preferredHeight: UISettings.iconSizeMedium
                Layout.preferredWidth: UISettings.bigItemHeight
                fontSize: UISettings.textSizeDefault * 0.9
                label: qsTr("Add all")
                enabled: browserListView.count > 0
                onClicked: if (modelProvider) modelProvider.addVisibleChannels()
            }
        }

        Rectangle
        {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: panelRoot.browserMinHeight
            color: UISettings.bgStrong
            border.width: 1
            border.color: UISettings.bgLight

            RobotoText
            {
                anchors.centerIn: parent
                visible: browserListView.count === 0
                labelColor: UISettings.fgLight
                label: qsTr("No matching channel")
            }

            ListView
            {
                id: browserListView
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                model: modelProvider ? modelProvider.browserFixtures : null

                delegate:
                    Column
                    {
                        id: fxDelegate
                        width: browserListView.width - (browserScrollBar.visible ? browserScrollBar.width : 0)

                        required property var modelData

                        /** Selected channels of this fixture, including the ones hidden by the filters */
                        readonly property int selectedCount: panelRoot.fixtureSelectedCount(modelData.fxID)
                        /** Selected channels of this fixture, among the listed ones */
                        readonly property int listedSelectedCount:
                            panelRoot.listedSelectedCount(modelData.fxID, modelData.matchIndices)
                        readonly property bool allListedSelected:
                            listedSelectedCount === modelData.matchIndices.length
                        readonly property bool expanded: panelRoot.isFixtureExpanded(modelData.fxID)

                        // fixture row
                        Rectangle
                        {
                            width: parent.width
                            height: UISettings.listItemHeight
                            color: fxDelegate.expanded ? UISettings.bgLight : "transparent"

                            RowLayout
                            {
                                anchors.fill: parent
                                spacing: 2

                                Text
                                {
                                    Layout.fillHeight: true
                                    Layout.preferredWidth: UISettings.listItemHeight
                                    color: UISettings.fgMain
                                    font.family: UISettings.fontAwesomeFontName
                                    font.pixelSize: UISettings.textSizeDefault * 0.8
                                    verticalAlignment: Text.AlignVCenter
                                    horizontalAlignment: Text.AlignHCenter
                                    text: fxDelegate.expanded ? FontAwesome.fa_chevron_down : FontAwesome.fa_chevron_right

                                    MouseArea
                                    {
                                        anchors.fill: parent
                                        onClicked: panelRoot.toggleFixtureExpanded(fxDelegate.modelData.fxID)
                                    }
                                }

                                CustomCheckBox
                                {
                                    implicitWidth: UISettings.listItemHeight
                                    implicitHeight: implicitWidth
                                    checked: fxDelegate.allListedSelected
                                    partiallyChecked: fxDelegate.listedSelectedCount > 0
                                    tooltip: qsTr("Add/Remove all the listed channels of this fixture")
                                    onClicked:
                                    {
                                        // the click toggles 'checked': restore the binding
                                        // and let the slider selection drive it again.
                                        // A partial selection is completed rather than cleared
                                        var select = !fxDelegate.allListedSelected
                                        checked = Qt.binding(function() { return fxDelegate.allListedSelected })

                                        if (modelProvider)
                                            modelProvider.setFixtureSelection(fxDelegate.modelData.fxID, select)
                                    }
                                }

                                IconTextEntry
                                {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    iSrc: fxDelegate.modelData.fxIcon
                                    tLabel: fxDelegate.modelData.fxName

                                    MouseArea
                                    {
                                        anchors.fill: parent
                                        onClicked: panelRoot.toggleFixtureExpanded(fxDelegate.modelData.fxID)
                                    }
                                }

                                RobotoText
                                {
                                    Layout.fillHeight: true
                                    visible: fxDelegate.selectedCount > 0
                                    labelColor: UISettings.selection
                                    fontSize: UISettings.textSizeDefault * 0.8
                                    label: fxDelegate.selectedCount + " " + qsTr("sel.")
                                }

                                RobotoText
                                {
                                    Layout.fillHeight: true
                                    labelColor: UISettings.fgLight
                                    fontSize: UISettings.textSizeDefault * 0.8
                                    label: fxDelegate.modelData.universe + "." + fxDelegate.modelData.address
                                }
                            }
                        }

                        // channel rows
                        Repeater
                        {
                            model: fxDelegate.expanded && modelProvider ?
                                       modelProvider.browserChannels(fxDelegate.modelData.fxID) : []

                            delegate:
                                Rectangle
                                {
                                    required property var modelData

                                    width: fxDelegate.width
                                    height: UISettings.listItemHeight
                                    color: "transparent"

                                    RowLayout
                                    {
                                        anchors.fill: parent
                                        anchors.leftMargin: UISettings.listItemHeight
                                        spacing: 2

                                        CustomCheckBox
                                        {
                                            implicitWidth: UISettings.listItemHeight
                                            implicitHeight: implicitWidth
                                            checked: panelRoot.isChannelSelected(modelData.fxID, modelData.chIdx)
                                            onClicked:
                                            {
                                                var fxID = modelData.fxID
                                                var chIdx = modelData.chIdx
                                                var select = !panelRoot.isChannelSelected(fxID, chIdx)

                                                // the click toggles 'checked': restore the binding
                                                // and let the slider selection drive it again
                                                checked = Qt.binding(function() { return panelRoot.isChannelSelected(fxID, chIdx) })

                                                if (modelProvider)
                                                    modelProvider.setChannelSelection(fxID, chIdx, select)
                                            }
                                        }

                                        IconTextEntry
                                        {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            iSrc: modelData.chIcon
                                            tLabel: (modelData.chIdx + 1) + ": " + modelData.chName
                                        }

                                        RobotoText
                                        {
                                            Layout.fillHeight: true
                                            labelColor: UISettings.fgLight
                                            fontSize: UISettings.textSizeDefault * 0.8
                                            label: modelData.universe + "." + modelData.dmxAddress
                                        }
                                    }
                                }
                        }
                    }

                ScrollBar.vertical: CustomScrollBar { id: browserScrollBar }
            }
        }
    }
}
