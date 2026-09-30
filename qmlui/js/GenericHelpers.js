/*
  Q Light Controller Plus
  GenericHelpers.js

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

function pluginIconFromName(name)
{
    switch (name)
    {
        case "ArtNet": return "qrc:/artnetplugin.svg";
        case "DMX USB": return "qrc:/dmxusbplugin.svg";
        case "HID": return "qrc:/hidplugin.svg";
        case "OLA": return "qrc:/olaplugin.svg";
        case "MIDI": return "qrc:/midiplugin.svg";
        case "OSC": return "qrc:/oscplugin.svg";
        case "E1.31": return "qrc:/e131plugin.svg";
        case "Loopback": return "qrc:/loop.svg";
        default: return "";
    }
}

function getHTMLColor(r, g, b)
{
    var color = r << 16 | g << 8 | b;
    var colStr = color.toString(16);
    return "#" + "000000".substr(0, 6 - colStr.length) + colStr;
}

/**
 * Returns the zoom direction asked for by a wheel event: 1 to zoom in,
 * -1 to zoom out and 0 when the event is not a zoom gesture at all.
 *
 * A zoom is asked for by scrolling while holding either Ctrl (as Audacity
 * and most other timeline editors do) or the middle mouse button. Every
 * other wheel event returns 0, so the caller can leave it to the views
 * that scroll on a plain wheel.
 */
function wheelZoomDirection(wheel)
{
    if (!(wheel.modifiers & Qt.ControlModifier) && !(wheel.buttons & Qt.MiddleButton))
        return 0;

    // high resolution trackpads and some mice only fill the pixel delta
    var delta = wheel.angleDelta.y;
    if (delta === 0)
        delta = wheel.pixelDelta.y;

    if (delta > 0)
        return 1;
    else if (delta < 0)
        return -1;

    return 0;
}

/**
 * Returns the time scale one zoom step away from the given one: whole units
 * from 1.0 up, tenths below it. growScale tells which way to go, which is
 * not the same as zooming in or out on every timeline (see zoomTimeline()
 * in ShowManager.qml).
 *
 * The result is not clamped: the ShowManager bounds it, and a step past a
 * bound simply leaves the scale as it is.
 */
function nextTimeScale(scale, growScale)
{
    if (growScale)
        return scale >= 1.0 ? scale + 1.0 : Math.round((scale + 0.1) * 10) / 10;

    return scale > 1.0 ? scale - 1.0 : Math.round((scale - 0.1) * 10) / 10;
}

/**
 * Returns the view offset that holds the position anchorX of a zoomable
 * view still while the zoom takes the view from oldRatio to newRatio pixels
 * per time unit: the point under anchorX stays at the same distance from
 * the left edge of the visible area. anchorX and viewOffset are in the
 * pixels of the old ratio, the result in the pixels of the new one, bounded
 * to [0, maxOffset].
 */
function zoomAnchoredOffset(anchorX, viewOffset, oldRatio, newRatio, maxOffset)
{
    if (oldRatio <= 0)
        return viewOffset;

    // both ratios are linear from the start of the view, so the new position
    // of the anchored point is a plain rescaling of the old one
    var newAnchorX = anchorX * (newRatio / oldRatio);
    return Math.max(0, Math.min(maxOffset, newAnchorX - (anchorX - viewOffset)));
}
