import QtQuick
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

// 4-ring interactive sunburst over sunburstModel. Tap a directory segment to
// focus it; hover highlights and shows a tooltip.
Item {
    id: root

    signal segmentClicked(int node, bool isDir, bool isOther)
    signal segmentHovered(int node, string name, string sizeText, bool isOther)

    property int hoverIndex: -1

    Canvas {
        id: canvas
        anchors.fill: parent
        antialiasing: true

        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            const w = width, h = height;
            const cx = w / 2, cy = h / 2;
            const maxR = Math.min(w, h) / 2 - 4;
            const innerR = maxR * 0.22; // hole for the focus node
            const ringW = (maxR - innerR) / 4;

            const n = sunburstModel.rowCount();
            for (let i = 0; i < n; ++i) {
                const idx = sunburstModel.index(i, 0);
                const ring = sunburstModel.data(idx, 258);      // RingRole
                const startF = sunburstModel.data(idx, 259);    // StartRole
                const endF = sunburstModel.data(idx, 260);      // EndRole
                const hue = sunburstModel.data(idx, 261);       // HueRole
                const isDir = sunburstModel.data(idx, 262);     // IsDirRole
                const isOther = sunburstModel.data(idx, 265);   // IsOtherRole

                const r0 = innerR + ring * ringW;
                const r1 = r0 + ringW - 1.5;

                // Fraction 0 at 12 o'clock, clockwise → radians.
                const a0 = startF * 2 * Math.PI - Math.PI / 2;
                const a1 = endF * 2 * Math.PI - Math.PI / 2;

                // Color: family hue, lightness by ring depth; gray for "other".
                let fill;
                if (isOther) {
                    fill = Qt.rgba(0.5, 0.5, 0.55, 0.35);
                } else {
                    const light = 0.62 - ring * 0.07;
                    const sat = isDir ? 0.55 : 0.30;
                    fill = Qt.hsla(hue, sat, light, i === root.hoverIndex ? 1.0 : 0.88);
                }

                ctx.beginPath();
                ctx.arc(cx, cy, r1, a0, a1);
                ctx.arc(cx, cy, r0, a1, a0, true);
                ctx.closePath();
                ctx.fillStyle = fill;
                ctx.fill();
                ctx.strokeStyle = "rgba(0,0,0,0.15)";
                ctx.lineWidth = 0.5;
                ctx.stroke();
            }

            // Center label: focus node name + size.
            ctx.fillStyle = Kirigami.Theme.textColor;
            ctx.textAlign = "center";
            ctx.textBaseline = "middle";
            ctx.font = "bold " + Math.round(innerR * 0.22) + "px sans-serif";
            const focusName = controller.breadcrumbs.length
                ? controller.breadcrumbs[controller.breadcrumbs.length - 1] : "";
            ctx.fillText(shorten(focusName, 14), cx, cy - innerR * 0.18);
            ctx.font = Math.round(innerR * 0.18) + "px sans-serif";
            ctx.globalAlpha = 0.7;
            ctx.fillText(controller.formatBytes(controller.nodeSize(controller.focusNode)),
                         cx, cy + innerR * 0.22);
            ctx.globalAlpha = 1.0;
        }

        function shorten(s, maxLen) {
            return s.length > maxLen ? s.slice(0, maxLen - 1) + "…" : s;
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true

            function polar(mouseX, mouseY) {
                const cx = canvas.width / 2, cy = canvas.height / 2;
                const maxR = Math.min(canvas.width, canvas.height) / 2 - 4;
                const innerR = maxR * 0.22;
                const ringW = (maxR - innerR) / 4;
                const dx = mouseX - cx, dy = mouseY - cy;
                const dist = Math.sqrt(dx * dx + dy * dy);
                if (dist < innerR || dist > maxR) return null;
                const ring = Math.floor((dist - innerR) / ringW);
                let angle = Math.atan2(dy, dx) + Math.PI / 2; // 0 at 12 o'clock
                if (angle < 0) angle += 2 * Math.PI;
                const fraction = angle / (2 * Math.PI);
                return { fraction: fraction, ring: ring };
            }

            function segmentAt(mouseX, mouseY) {
                const p = polar(mouseX, mouseY);
                if (!p) return -1;
                const n = sunburstModel.rowCount();
                for (let i = 0; i < n; ++i) {
                    const idx = sunburstModel.index(i, 0);
                    if (sunburstModel.data(idx, 258) !== p.ring) continue;
                    const s = sunburstModel.data(idx, 259);
                    const e = sunburstModel.data(idx, 260);
                    if (p.fraction >= s && p.fraction < e) return i;
                }
                return -1;
            }

            onPositionChanged: mouse => {
                const i = segmentAt(mouse.x, mouse.y);
                if (i !== root.hoverIndex) {
                    root.hoverIndex = i;
                    canvas.requestPaint();
                }
                if (i >= 0) {
                    const idx = sunburstModel.index(i, 0);
                    const node = sunburstModel.data(idx, 257);
                    const name = sunburstModel.data(idx, 264);
                    const bytes = sunburstModel.data(idx, 263);
                    const isOther = sunburstModel.data(idx, 265);
                    root.segmentHovered(node, name, controller.formatBytes(bytes), isOther);
                } else {
                    root.segmentHovered(-1, "", "", false);
                }
            }

            onClicked: mouse => {
                const i = segmentAt(mouse.x, mouse.y);
                if (i < 0) return;
                const idx = sunburstModel.index(i, 0);
                root.segmentClicked(sunburstModel.data(idx, 257),
                                    sunburstModel.data(idx, 262),
                                    sunburstModel.data(idx, 265));
            }

            onExited: {
                root.hoverIndex = -1;
                canvas.requestPaint();
                root.segmentHovered(-1, "", "", false);
            }
        }
    }

    // Repaint whenever the model resets (graph/lens/focus change).
    Connections {
        target: sunburstModel
        function onModelReset() { canvas.requestPaint(); }
    }
    Connections {
        target: controller
        function onLensChanged() { canvas.requestPaint(); }
    }
}
