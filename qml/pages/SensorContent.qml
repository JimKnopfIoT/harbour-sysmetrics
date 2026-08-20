import QtQuick 2.0
import Sailfish.Silica 1.0
import QtSensors 5.0
import QtPositioning 5.0
import "../components"

// Loaded via Loader from SensorsPage; if a QML plugin is missing this file
// fails to load and the shell shows a fallback instead.
Item {
    id: root
    anchors.fill: parent

    property bool live: Qt.application.active
    property real gpsStartMs: 0
    property real ttff: -1
    function fmt(v, d) { return (v === undefined) ? "—" : v.toFixed(d === undefined ? 2 : d) }

    Accelerometer { id: accel; active: root.live }
    Gyroscope { id: gyro; active: root.live }
    Magnetometer { id: mag; active: root.live }
    Compass { id: compass; active: root.live }
    ProximitySensor { id: prox; active: root.live }
    AmbientLightSensor { id: light; active: root.live }
    RotationSensor { id: rot; active: root.live }

    PositionSource {
        id: gps
        active: root.live && gpsSwitch.checked
        updateInterval: 1000
        onActiveChanged: if (active) { root.gpsStartMs = Date.now(); root.ttff = -1 }
        onPositionChanged: {
            if (position.latitudeValid && root.ttff < 0)
                root.ttff = (Date.now() - root.gpsStartMs) / 1000
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: col.height

        Column {
            id: col
            width: root.width
            spacing: Theme.paddingMedium

            PageHeader { title: qsTr("Sensors & GPS") }

            SectionHeader { text: qsTr("Motion & orientation") }
            Column {
                x: Theme.horizontalPageMargin
                width: root.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall / 2
                KeyValue { opacity: accel.connectedToBackend ? 1 : 0.4; label: qsTr("Accelerometer"); mono: true
                    value: accel.connectedToBackend && accel.reading
                        ? root.fmt(accel.reading.x) + ", " + root.fmt(accel.reading.y) + ", " + root.fmt(accel.reading.z) + " m/s²" : qsTr("not available") }
                KeyValue { opacity: gyro.connectedToBackend ? 1 : 0.4; label: qsTr("Gyroscope"); mono: true
                    value: gyro.connectedToBackend && gyro.reading
                        ? root.fmt(gyro.reading.x) + ", " + root.fmt(gyro.reading.y) + ", " + root.fmt(gyro.reading.z) + " °/s" : qsTr("not available") }
                KeyValue { opacity: rot.connectedToBackend ? 1 : 0.4; label: qsTr("Rotation (pitch/roll/yaw)"); mono: true
                    value: rot.connectedToBackend && rot.reading
                        ? root.fmt(rot.reading.x, 0) + "°, " + root.fmt(rot.reading.y, 0) + "°, " + root.fmt(rot.reading.z, 0) + "°" : qsTr("not available") }
                KeyValue { opacity: compass.connectedToBackend ? 1 : 0.4; label: qsTr("Compass azimuth")
                    value: compass.connectedToBackend && compass.reading
                        ? root.fmt(compass.reading.azimuth, 0) + "°  (" + qsTr("calib. %1").arg(root.fmt(compass.reading.calibrationLevel, 1)) + ")" : qsTr("not available") }
                KeyValue { opacity: mag.connectedToBackend ? 1 : 0.4; label: qsTr("Magnetometer"); mono: true
                    value: mag.connectedToBackend && mag.reading
                        ? root.fmt(mag.reading.x * 1e6, 0) + ", " + root.fmt(mag.reading.y * 1e6, 0) + ", " + root.fmt(mag.reading.z * 1e6, 0) + " µT" : qsTr("not available") }
            }

            SectionHeader { text: qsTr("Environment") }
            Column {
                x: Theme.horizontalPageMargin
                width: root.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall / 2
                KeyValue { opacity: prox.connectedToBackend ? 1 : 0.4; label: qsTr("Proximity")
                    value: prox.connectedToBackend && prox.reading ? (prox.reading.near ? qsTr("near") : qsTr("far")) : qsTr("not available") }
                KeyValue { opacity: light.connectedToBackend ? 1 : 0.4; label: qsTr("Ambient light")
                    value: light.connectedToBackend && light.reading ? light.reading.lightLevel + " (" + qsTr("level") + ")" : qsTr("not available") }
            }

            SectionHeader { text: qsTr("GPS / positioning") }
            TextSwitch { id: gpsSwitch; text: qsTr("Enable GPS")
                description: qsTr("Starts the positioning hardware; costs battery.") }
            Column {
                x: Theme.horizontalPageMargin
                width: root.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall / 2
                visible: gpsSwitch.checked
                KeyValue { label: qsTr("Status"); value: gps.valid ? qsTr("fix acquired") : qsTr("searching …")
                    valueColor: gps.valid ? Diag.green : Diag.amber }
                KeyValue { label: qsTr("Time to first fix"); value: root.ttff >= 0 ? root.ttff.toFixed(1) + " s" : "—" }
                KeyValue { label: qsTr("Latitude"); mono: true
                    value: gps.position.latitudeValid ? gps.position.coordinate.latitude.toFixed(6) : "—" }
                KeyValue { label: qsTr("Longitude"); mono: true
                    value: gps.position.longitudeValid ? gps.position.coordinate.longitude.toFixed(6) : "—" }
                KeyValue { label: qsTr("Altitude"); value: gps.position.altitudeValid ? gps.position.coordinate.altitude.toFixed(0) + " m" : "—" }
                KeyValue { label: qsTr("Accuracy"); value: gps.position.horizontalAccuracyValid ? "± " + gps.position.horizontalAccuracy.toFixed(0) + " m" : "—" }
                KeyValue { label: qsTr("Speed"); value: gps.position.speedValid ? (gps.position.speed * 3.6).toFixed(1) + " km/h" : "—" }
                Label { width: parent.width; visible: gps.sourceError !== PositionSource.NoError
                    text: qsTr("Positioning error — is Location enabled in system settings?")
                    wrapMode: Text.Wrap; font.pixelSize: Theme.fontSizeTiny; color: Diag.amber }
            }
            Item { width: 1; height: Theme.paddingLarge }
        }
        VerticalScrollDecorator {}
    }
}
