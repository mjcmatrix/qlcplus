/*
  Q Light Controller Plus
  PixelBar3DItem.qml

  Copyright (c) Massimo Callegari
  Copyright (c) Eric Arnebäck

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

import QtQuick as QQ2

import Qt3D.Core
import Qt3D.Render
import Qt3D.Extras

import org.qlcplus.classes 1.0
import "Math3DView.js" as Math3D
import "."

Entity
{
    id: fixtureEntity
    objectName: "fixture3DItem"

    property int itemID: fixtureManager.invalidFixture()
    property bool isSelected: false
    property int headsNumber: 0
    property size headsLayout: Qt.size(1, 1)
    property vector3d phySize: Qt.vector3d(1, 0.1, 0.1)
    property real shutterValue: sAnimator.shutterValue
    /* Luminous intensity of a single emitter of this fixture, in candela: the
       "Lumens" physical property of its mode spread over the solid angle of the
       beam at the widest the lens opens. 0 when the definition has no data */
    property real bulbCandela: 0
    /* Relative output of this fixture: its intensity against the brightest
       emitter in the project, so the reference fixture stays at the brightness
       it has always rendered at and everything else falls in around it. 1.0
       (unscaled) when the "Lumens" setting is off, when this definition has no
       lumens, or when no fixture in the project has any. */
    property real lumensScale:
        (View3D && View3D.useFixtureLumens && bulbCandela > 0 && View3D.referenceCandela > 0) ?
            bulbCandela / View3D.referenceCandela : 1.0


    onItemIDChanged:
    {
        isSelected = contextManager.isFixtureSelected(itemID)
        headsRepeater.model = headsNumber
        updateHeads()
    }

    /* Number of cells across the bar width and along its depth. Same guard as
       MultiBeams3DItem: the physical layout describes the cell grid of the
       fixture body and is not required to agree with the number of heads the
       selected mode declares, so only trust it when it accounts for exactly
       the cells we have; otherwise put them in one row. */
    readonly property bool layoutMatchesHeads:
        headsLayout.width * headsLayout.height === headsNumber
    readonly property int cellColumns:
        layoutMatchesHeads ? headsLayout.width : Math.max(1, headsNumber)
    readonly property int cellRows:
        layoutMatchesHeads ? headsLayout.height : 1

    /* **************** Focus properties **************** */
    /* A pixel bar is a wash device: battens and blinders of this type carry
       lenses of 40 degrees and up, and the Jolt style blinders 120. Default
       wide, unlike the 15-30 degrees a beam fixture falls back to, so a
       definition that gives no lens angle still washes rather than spotlights. */
    property real focusMinDegrees: 120
    property real focusMaxDegrees: 120
    property real distCutoff: 40.0

    /* Aperture the definition declares, as a full angle in degrees. By stage
       convention this is the BEAM angle: the full width at 50% of peak
       intensity, with the field angle - the width at 10% - roughly half again
       as wide. The renderer's cone is a top hat, so taking the declared figure
       as the cone edge draws the half power width at full power and then stops
       dead, which is why a wash bar came out looking like a focused beam. */
    property real beamDegrees: focusMinDegrees
    readonly property real beamHalfAngle: (beamDegrees / 2) * (Math.PI / 180)

    /* Ratio of field angle to beam angle for a wash optic. */
    readonly property real fieldAngleRatio: 1.5

    /* Widest cone this renderer draws well. getLightProjectionMatrix() builds a
       perspective frustum from the cone half angle and the shadow map covers
       exactly that frustum, so a very wide cone spreads 1024 texels over most of
       a hemisphere and the beam starts failing its own shadow test. 60 degrees
       is the widest aperture already in use, so it is known good: beyond it the
       aperture is left alone and only the falloff below widens. */
    readonly property real maxHalfAngle: 60 * (Math.PI / 180)

    /* Draw the cone out to the field angle, so light reaches as far as it
       really does, and let beamEdgeSoftness put the 50% point back on the
       declared beam angle. */
    readonly property real fieldHalfAngle:
        Math.min(beamHalfAngle * fieldAngleRatio, maxHalfAngle)

    property real cutoffAngle: fieldHalfAngle

    /* An aperture this wide is not a projected beam at all. SMD pixel battens
       publish the LED's viewing angle - the half power cone of a bare emitter,
       typically 110 to 120 degrees - and such an emitter is essentially
       Lambertian: it radiates across the hemisphere with a cosine falloff and
       has no rim whatsoever. */
    readonly property real viewingAngleThreshold: 90

    /* Softness that puts the half power point back on the declared beam angle.
       beamPenumbra() fades over a fraction of the cone RADIUS, so the crossover
       is a ratio of tangents rather than of angles: solving
       smoothstep(1 - soft, 1, ratio) = 0.5 gives soft = 2 - 2 * ratio. */
    readonly property real beamEdgeSoftness:
    {
        if (beamHalfAngle <= 0)
            return 0

        if (beamDegrees >= viewingAngleThreshold)
            return 1.0

        var ratio = Math.tan(beamHalfAngle) / Math.tan(fieldHalfAngle)
        return Math.max(0.0, Math.min(1.0, 2.0 - (2.0 * ratio)))
    }

    /* **************** Rendering quality properties **************** */
    /* The whole point of this fixture type: it lights the surfaces it is aimed
       at, but draws no beam in the air. A batten or a blinder reads as a lit
       wall or a lit floor, not as a visible shaft, which is also what separates
       the "Pixels" bar icon from the "Beams" one. Leaving useScattering false
       keeps the cost of a bar to one shadow map and one shading pass per cell,
       with no ray marching at all. */
    property bool useShading: View3D.renderQuality === MainView3D.LowQuality ? false : true
    property bool useScattering: false

    /* Shadows are not optional: spotlight_shading.frag bounds the light with
       the emitter's shadow map and nothing else, so a cell without one lights
       everything inside its cone projection, straight through the walls of the
       stage environment. RenderShadowMapFilter matches no render pass while a
       cell is dark, so an unlit bar costs nothing. */
    property bool useShadows: View3D.renderQuality === MainView3D.LowQuality ? false : true

    /* This item never enters the scattering pass, so no ray marching is done
       for it. The property exists because LightEntity binds it into the
       "raymarchSteps" uniform regardless. */
    readonly property int raymarchSteps: 0

    /* **************** Spotlight cone properties **************** */
    /* Radius of a single cell. The 0.7 factor matches Fixture3DItem, where it
       compensates the mesh lens being slightly larger than the emitting surface. */
    property real coneTopRadius:
        Math.max(0.005, 0.5 * 0.7 * Math.min(phySize.x / cellColumns, phySize.z / cellRows))
    property real coneBottomRadius: distCutoff * Math.tan(cutoffAngle) + coneTopRadius

    /* Depth of the emitter inside the fixture body. The emitters sit ON the
       emitting face (see setHeadLightProps), so there is no housing in front of
       them to clear - unlike Fixture3DItem, where the lens is recessed into a
       loaded mesh. Keep it small but non-zero: it offsets the near plane of the
       light frustum, and zero puts that plane exactly on the emitter. */
    property real headLength: Math.max(0.01, phySize.y * 0.25)

    /* ********************* Light properties ********************* */
    property vector3d lightDir: Math3D.getLightDirection(transform, null, null)

    property var headsList: []

    function updateHeads()
    {
        var i

        // Delete the existing emitters first. setupScattering() re-parents the
        // three cones of an emitter to the scene root, so they do not die with
        // it: without cleanupScattering() they would be left in the scene, still
        // bound to a destroyed emitter, and every rebuild would add three more.
        for (i = headsList.length - 1; i >= 0; i--)
        {
            headsList[i].cleanupScattering()
            headsList[i].destroy()
        }

        headsList = []

        // itemID is invalid while the item is being built - MainView3D sets the
        // real one right after creation - and MainView3D::resetItems() sets it
        // back to -1 when the 3D view is torn down. Building emitters in either
        // case is pure waste, and in the teardown case it allocates them into a
        // scene that is being destroyed.
        if (itemID < 0 || headsNumber <= 0)
            return

        // Component is qualified because this file imports QtQuick under the
        // QQ2 namespace, to keep QtQuick's Transform from colliding with the
        // Qt3D.Core one. MultiBeams3DItem imports QtQuick unqualified and so
        // spells the same check "Component.Ready".
        var component = Qt.createComponent("LightEntity.qml")
        if (component.status !== QQ2.Component.Ready)
        {
            console.warn("PixelBar3DItem: cannot load LightEntity.qml:", component.errorString())
            return
        }

        for (i = 0; i < headsNumber; i++)
        {
            // Everything shared with the parent is bound rather than copied:
            // most of these are only known once MainView3D::initializeFixture()
            // has run, which happens below, and render quality, zoom and shutter
            // keep changing afterwards.
            var headNode = component.createObject(fixtureEntity,
            {
                "headIndex": i,
                "enabled": Qt.binding(function() { return fixtureEntity.enabled }),
                "lightDir": Qt.binding(function() { return fixtureEntity.lightDir }),
                "shutterValue": Qt.binding(function() { return fixtureEntity.shutterValue }),
                "lumensScale": Qt.binding(function() { return fixtureEntity.lumensScale }),
                "raymarchSteps": Qt.binding(function() { return fixtureEntity.raymarchSteps }),
                "cutoffAngle": Qt.binding(function() { return fixtureEntity.cutoffAngle }),
                "distCutoff": Qt.binding(function() { return fixtureEntity.distCutoff }),
                "headLength": Qt.binding(function() { return fixtureEntity.headLength }),
                "coneTopRadius": Qt.binding(function() { return fixtureEntity.coneTopRadius }),
                "beamEdgeSoftness": Qt.binding(function() { return fixtureEntity.beamEdgeSoftness }),
                "goboTexture": Qt.binding(function() { return fixtureEntity.goboTexture })
            });

            if (headNode === null)
            {
                console.warn("PixelBar3DItem: cannot create cell", i, "of item", itemID)
                break
            }

            headsList.push(headNode)
        }

        // 3DView.qml walks headsNumber heads when it builds the frame graph, so
        // this must never promise more emitters than actually exist
        if (headsList.length !== headsNumber)
            headsNumber = headsList.length

        // initializeFixture() is what hands us phySize and the lens angles, so
        // the cone geometry logged below is only meaningful once it has returned
        View3D.initializeFixture(itemID, fixtureEntity, null)

        console.log("PixelBar3DItem: item", itemID, "cells:", headsList.length,
                    "layout:", headsLayout.width + "x" + headsLayout.height,
                    "used as:", cellColumns + "x" + cellRows,
                    "| lens:", focusMinDegrees + "-" + focusMaxDegrees + " deg",
                    "cone top/bottom:", coneTopRadius.toFixed(4) + "/" + coneBottomRadius.toFixed(2),
                    "| field:", (fieldHalfAngle * 2 * 180 / Math.PI).toFixed(0) + " deg",
                    "softness:", beamEdgeSoftness.toFixed(2))
    }

    function setupScattering(sceneEntity)
    {
        if (sceneEntity.coneMesh.length !== distCutoff)
            sceneEntity.coneMesh.length = distCutoff

        for (var i = 0; i < headsList.length; i++)
            headsList[i].setupScattering(sceneEntity)
    }

    function cleanupScattering()
    {
        for (var i = 0; i < headsList.length; i++)
        {
            var headItem = headsList[i]
            if (headItem && headItem.cleanupScattering)
                headItem.cleanupScattering()
        }
    }

    function getHead(headIndex)
    {
        if (headIndex < 0 || headIndex >= headsList.length)
            return null

        return headsList[headIndex]
    }

    // The C++ side computes a single emitter position and orientation for the
    // whole bar (headIndex is always 0), so spread the cells over the fixture
    // body here: evenly across its width and depth, centered on the origin and
    // rotated by the bar's current orientation matrix. Same as MultiBeams3DItem,
    // and it must stay the same, because the emissive cell planes below are laid
    // out on the identical grid.
    function setHeadLightProps(headIndex, pos, matrix)
    {
        var count = headsList.length
        if (count === 0)
            return

        var cellWidth = phySize.x / cellColumns
        var cellDepth = phySize.z / cellRows

        for (var h = 0; h < count; h++)
        {
            var column = h % cellColumns
            var row = Math.floor(h / cellColumns)
            // On the emitting face, not the middle of the bar. The housing is
            // drawn as one cuboid spanning the full phySize.y, so an emitter at
            // the centre sits INSIDE it and the shadow map records the bar's own
            // underside as the first surface the light meets - every cell then
            // fails its own shadow test and the bar lights nothing. (The beam bar
            // gets away with a centred emitter because its body is two half
            // height cuboids, so the centre is already on a face.)
            var localPos = Qt.vector4d(-(phySize.x / 2) + ((column + 0.5) * cellWidth),
                                       -(phySize.y / 2),
                                       -(phySize.z / 2) + ((row + 0.5) * cellDepth), 0)

            var head = headsList[h]
            head.lightPos = pos.plus(matrix.times(localPos).toVector3d())
            head.lightMatrix = matrix
        }
    }

    /* A cell is two things that have to move together: the emitter that lights
       the room, and the glowing patch on the housing that shows which pixel is
       lit. Drive both from every value update. */
    function setHeadIntensity(headIndex, intensity)
    {
        if (headIndex >= 0 && headIndex < headsList.length)
            headsList[headIndex].dimmerValue = intensity

        var plane = headsRepeater.objectAt(headIndex)
        if (plane)
            plane.dimmerValue = intensity
    }

    function setHeadRGBColor(headIndex, color)
    {
        if (headIndex >= 0 && headIndex < headsList.length)
            headsList[headIndex].lightColor = color

        var plane = headsRepeater.objectAt(headIndex)
        if (plane)
            plane.lightColor = color
    }

    function setShutter(type, low, high)
    {
        sAnimator.setShutter(type, low, high)
    }

    // Same signature as Fixture3DItem: MainView3D calls this with degrees == true
    // when the fixture has a fixed zoom set in the monitor properties
    // Zoom sets the BEAM angle; the cone aperture and the edge softness are
    // derived from it above, so a zoom move keeps the two consistent.
    function setZoom(value, degrees)
    {
        if (degrees)
            beamDegrees = value
        else
            beamDegrees = (((focusMaxDegrees - focusMinDegrees) / 255.0) * value) + focusMinDegrees
    }

    ShutterAnimator { id: sAnimator }

    /* Main transform of the whole fixture item */
    property Transform transform: Transform { }

    property Layer sceneLayer
    property Effect sceneEffect

    property Texture2D goboTexture:
        Texture2D
        {
            // sampled at whatever resolution the light happens to cover, so it
            // needs filtering: the Qt3D default of Nearest re-introduces the
            // stair steps the mask is painted smooth to avoid
            magnificationFilter: Texture.Linear
            minificationFilter: Texture.Linear
        }

    property Material material:
        Material
        {
            effect: sceneEffect

            parameters: [
                Parameter { name: "diffuse"; value: Qt.color("gray") },
                Parameter { name: "specular"; value: Qt.color("black") },
                Parameter { name: "shininess"; value: 1.0 },
                Parameter { name: "bloom"; value: 0 }
            ]
        }

    CuboidMesh
    {
        id: baseMesh
        xExtent: phySize.x
        yExtent: phySize.y
        zExtent: phySize.z
    }

    /* MainView3D::initializeFixture() looks this up by name to push the lens
       angles down and to call setupScattering(), and updateLightMatrix() needs
       it to place the emitters. A pixel bar does not tilt, so unlike the beam
       bar's head this carries no geometry of its own and does not move. */
    Entity
    {
        id: headEntity
        objectName: "headEntity"

        property Transform headTransform: Transform { }

        components: [ headTransform ]
    }

    NodeInstantiator
    {
        id: headsRepeater
        //model: fixtureEntity.headsNumber

        delegate:
            Entity
            {
                id: headDelegate
                property real dimmerValue: 0
                /* Brightness of the lit patch on the housing: the LED array as
                   the viewer sees it head on, which is a different quantity from
                   the light the cell throws into the room.

                   What the eye reads off a source it looks straight at is the
                   source's luminance, and that is a property of the emitter
                   alone: it does not fall off with distance and it does not
                   depend on what else is rigged. So this deliberately carries
                   neither lumensScale nor the "Fixture light" gain, both of
                   which belong to radiated light and are applied to the per cell
                   LightEntity emitters instead. Carrying them here scaled the lit
                   face by the project wide candela ratio - 0.014 for a batten
                   rigged alongside a narrow beam moving head - and a bar whose
                   pixels are plainly on rendered as a dark grey box. */
                property real lightIntensity: dimmerValue * shutterValue
                property real headWidth: phySize.x / cellColumns
                property real headHeight: phySize.z / cellRows
                property color lightColor: Qt.rgba(0, 0, 0, 1)

                enabled: lightIntensity === 0 || lightColor === Qt.rgba(0, 0, 0, 1) ? false : true

                PlaneMesh
                {
                    id: headMesh
                    width: headWidth
                    height: headHeight
                    meshResolution: Qt.size(2, 2)
                }

                /* On the underside of the housing, because that is the face the
                   light leaves by: Math3DView.getLightDirection() aims every
                   fixture type down its local -Y. While a pixel bar cast no
                   light at all the two could disagree unnoticed, but a bar whose
                   pixels glow upwards while it lights the floor reads as broken. */
                property Transform headTransform:
                    Transform
                    {
                        /* Face the plane down its fixture's -Y, the way the light
                           leaves. A PlaneMesh is a single quad whose normal points
                           +Y, and Qt3D culls back faces by default, so an unrotated
                           cell was drawn facing up into the housing and clipped away
                           from every angle it could have been seen from. Turning it
                           over also makes the normal it writes into the G buffer the
                           direction the cell actually emits in. */
                        rotationX: 180

                        translation: {
                            var row = Math.floor(index / cellColumns)
                            var column = index % cellColumns
                            var xPos = (column * headWidth) + (headWidth / 2)
                            var zPos = (row * headHeight) + (headHeight / 2)

                            return Qt.vector3d(-(phySize.x / 2) + xPos, -(phySize.y / 2) - 0.001, -(phySize.z / 2) + zPos)
                        }
                    }

                property Material headMaterial:
                    Material
                    {
                        effect: sceneEffect

                        parameters: [
                            Parameter {
                                name: "diffuse"
                                value: Qt.rgba(lightColor.r * lightIntensity, lightColor.g * lightIntensity, lightColor.b * lightIntensity, 1)
                            },
                            Parameter { name: "specular"; value: Qt.color("black") },
                            Parameter { name: "shininess"; value: 1.0 },
                            Parameter { name: "bloom"; value: 1 }
                        ]
                    }

                components: [
                    headMesh,
                    headTransform,
                    headMaterial,
                    fixtureEntity.sceneLayer
                ]
            }
    }

    components: [
        baseMesh,
        transform,
        material,
        sceneLayer
    ]
}
