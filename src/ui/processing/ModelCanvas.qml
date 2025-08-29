import QtQuick 2.15

Item {

    width: 100
    height: 100
    Text {
        id: helloworld
        text: qsTr("helloworld")
    }
    Rectangle
    {
        id:topRect
        // anchors.fill: parent
        width: 100
        height: 100
        color: "#201d1d"
    }
}


// Window {
//     width: 640
//     height: 480
//     visible: true
//     title: qsTr("Hello World")

//     Rectangle
//     {
//         id: rect
//         width: 100
//         height: 100
//         color: "red"

//         MouseArea{
//             anchors.fill: parent
//             onClicked: console.log("click")
//             drag.target: rect
//         }
//     }
// }
