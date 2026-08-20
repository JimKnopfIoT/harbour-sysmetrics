import QtQuick 2.0
import Sailfish.Silica 1.0

// Static dark gradient, rendered once into a cached layer.
Rectangle {
    anchors.fill: parent
    gradient: Gradient {
        GradientStop { position: 0.0; color: "#1b2029" }
        GradientStop { position: 0.6; color: "#12161d" }
        GradientStop { position: 1.0; color: "#0b0e13" }
    }
    layer.enabled: true
}
