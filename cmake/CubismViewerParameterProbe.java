import java.awt.AWTEvent;
import java.awt.Component;
import java.awt.Container;
import java.awt.IllegalComponentStateException;
import java.awt.Rectangle;
import java.awt.Toolkit;
import java.awt.Window;
import java.awt.event.MouseEvent;
import java.io.BufferedWriter;
import java.io.IOException;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ConcurrentLinkedQueue;
import javax.swing.JTree;
import javax.swing.SwingUtilities;
import javax.swing.tree.TreePath;

public final class CubismViewerParameterProbe {
    private static final String VIEWER =
        "com.live2d.cubism.doc.modeling.ui.viewerForOriginalWorkflow.OWViewerDropFrame";
    private static final long START_NS = System.nanoTime();
    private static final ConcurrentLinkedQueue<MouseRecord> EVENTS =
        new ConcurrentLinkedQueue<>();
    private static volatile Object viewerWindow;
    private static volatile Object uiModel;
    private static volatile Object dataModel;
    private static volatile Object sdkModel;
    private static volatile Object targetPoint;
    private static volatile boolean ready;

    private CubismViewerParameterProbe() {}

    private static final class MouseRecord {
        final long timeNs = System.nanoTime();
        final int id;
        final long when;
        final String source;
        final Rectangle bounds;
        final int x;
        final int y;
        final int screenX;
        final int screenY;

        MouseRecord(MouseEvent event) {
            id = event.getID();
            when = event.getWhen();
            Component component = event.getComponent();
            source = component == null ? "" : component.getClass().getName();
            bounds = component == null ? new Rectangle() : component.getBounds();
            x = event.getX();
            y = event.getY();
            int sx = Integer.MIN_VALUE;
            int sy = Integer.MIN_VALUE;
            try {
                sx = event.getXOnScreen();
                sy = event.getYOnScreen();
            } catch (IllegalComponentStateException ignored) {
                // A component can disappear while a queued event is inspected.
            }
            screenX = sx;
            screenY = sy;
        }
    }

    private static Object invoke(Object target, String name) throws Exception {
        Method method = target.getClass().getMethod(name);
        return method.invoke(target);
    }

    private static Object field(Object target, String name) throws Exception {
        Field field = target.getClass().getDeclaredField(name);
        field.setAccessible(true);
        return field.get(target);
    }

    private static boolean findModel() {
        try {
            Class<?> viewerClass = Class.forName(VIEWER);
            Field win = viewerClass.getDeclaredField("win");
            win.setAccessible(true);
            viewerWindow = win.get(null);
            if (viewerWindow == null) return false;
            Object models = invoke(viewerWindow, "getUiModelMgr");
            if (!(models instanceof List<?>) || ((List<?>)models).isEmpty())
                return false;
            uiModel = ((List<?>)models).get(0);
            dataModel = invoke(uiModel, "e");
            if (dataModel == null) return false;
            sdkModel = invoke(dataModel, "getSdkForJavaModel");
            targetPoint = field(invoke(uiModel, "b"), "f");
            return sdkModel != null && targetPoint != null;
        } catch (ReflectiveOperationException | RuntimeException error) {
            return false;
        } catch (Exception error) {
            return false;
        }
    }

    private static boolean prepareStaticModel() {
        try {
            if (!Boolean.TRUE.equals(invoke(dataModel, "isInitialized")))
                return false;
            Object viewer = invoke(uiModel, "a");
            Object frame = invoke(viewer, "c");
            Object automaticMotion = invoke(frame, "o");
            Method selected = automaticMotion.getClass().getMethod(
                "setSelected", boolean.class);
            selected.invoke(automaticMotion, false);
            Object updateSystem = invoke(uiModel, "b");
            invoke(invoke(updateSystem, "a"), "d");
            invoke(dataModel, "setDefaultParameterValue");
            System.out.println("CUBISM_VIEWER_PROBE_STATIC");
            return true;
        } catch (Exception error) {
            return false;
        }
    }

    private static void inspectTrees(Component component) {
        if (component instanceof JTree) {
            JTree tree = (JTree)component;
            System.out.println("CUBISM_VIEWER_TREE rows=" + tree.getRowCount() +
                " class=" + tree.getClass().getName());
            for (int row = 0; row < tree.getRowCount(); ++row) {
                TreePath path = tree.getPathForRow(row);
                System.out.println("CUBISM_VIEWER_TREE_ROW row=" + row +
                    " path=" + String.valueOf(path));
            }
        }
        if (component instanceof Container)
            for (Component child : ((Container)component).getComponents())
                inspectTrees(child);
    }

    private static void inspectTrees() {
        try {
            SwingUtilities.invokeAndWait(() -> {
                System.out.println("CUBISM_VIEWER_UI manager=" +
                    viewerWindow.getClass().getName());
                for (Window window : Window.getWindows()) {
                    System.out.println("CUBISM_VIEWER_WINDOW class=" +
                        window.getClass().getName() + " visible=" +
                        window.isVisible());
                    inspectTrees(window);
                }
            });
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
        } catch (InvocationTargetException error) {
            error.getCause().printStackTrace();
        }
    }

    private static boolean activateExpression(Component component, int index) {
        if (component instanceof JTree) {
            JTree tree = (JTree)component;
            TreePath expressions = null;
            for (int row = 0; row < tree.getRowCount(); ++row) {
                TreePath path = tree.getPathForRow(row);
                if (path != null && "expressions".equals(
                    String.valueOf(path.getLastPathComponent()))) {
                    expressions = path;
                    break;
                }
            }
            if (expressions != null) {
                Object group = expressions.getLastPathComponent();
                int children = tree.getModel().getChildCount(group);
                System.out.println("CUBISM_VIEWER_EXPRESSIONS children=" +
                    children);
                if (index < 0 || index >= children) return false;
                Object item = tree.getModel().getChild(group, index);
                TreePath path = expressions.pathByAddingChild(item);
                tree.expandPath(expressions);
                Rectangle bounds = tree.getPathBounds(path);
                if (bounds == null) return false;
                tree.setSelectionPath(path);
                tree.dispatchEvent(new MouseEvent(tree,
                    MouseEvent.MOUSE_CLICKED, System.currentTimeMillis(),
                    0, bounds.x + 8, bounds.y + bounds.height / 2,
                    2, false, MouseEvent.BUTTON1));
                System.out.println("CUBISM_VIEWER_EXPRESSION index=" +
                    index + " path=" + path);
                return true;
            }
        }
        if (component instanceof Container)
            for (Component child : ((Container)component).getComponents())
                if (activateExpression(child, index)) return true;
        return false;
    }

    private static boolean activateExpression(int index) {
        boolean[] activated = {false};
        try {
            SwingUtilities.invokeAndWait(() -> {
                for (Window window : Window.getWindows())
                    if (!activated[0])
                        activated[0] = activateExpression(window, index);
            });
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
        } catch (InvocationTargetException error) {
            error.getCause().printStackTrace();
        }
        return activated[0];
    }

    private static LinkedHashMap<String, Float> parameters() throws Exception {
        Object values = invoke(sdkModel, "g");
        LinkedHashMap<String, Float> result = new LinkedHashMap<>();
        for (Map.Entry<?, ?> entry : ((Map<?, ?>)values).entrySet()) {
            Object value = entry.getValue();
            if (entry.getKey() != null && value instanceof Number)
                result.put(entry.getKey().toString(),
                    ((Number)value).floatValue());
        }
        return result;
    }

    private static double[] look() throws Exception {
        String[] names = {"b", "c", "d", "e", "f", "g", "h"};
        double[] result = new double[names.length];
        for (int i = 0; i < names.length; ++i) {
            Field field = targetPoint.getClass().getDeclaredField(names[i]);
            field.setAccessible(true);
            result[i] = i == names.length - 1 ? field.getLong(targetPoint) :
                field.getFloat(targetPoint);
        }
        return result;
    }

    private static boolean changed(Map<String, Float> before,
        Map<String, Float> after) {
        if (before.size() != after.size()) return true;
        for (Map.Entry<String, Float> entry : after.entrySet()) {
            Float old = before.get(entry.getKey());
            if (old == null || Float.floatToIntBits(old) !=
                Float.floatToIntBits(entry.getValue())) return true;
        }
        return false;
    }


    private static boolean changed(double[] before, double[] after) {
        for (int i = 0; i < before.length; ++i)
            if (Double.doubleToLongBits(before[i]) !=
                Double.doubleToLongBits(after[i])) return true;
        return false;
    }

    private static String csv(String value) {
        if (value == null) return "";
        return '"' + value.replace("\"", "\"\"") + '"';
    }

    private static void header(BufferedWriter output, Iterable<String> ids)
        throws IOException {
        output.write("elapsed_ms,kind,mouse_id,event_when_ms,source," +
            "component_x,component_y,component_width,component_height," +
            "mouse_x,mouse_y,screen_x,screen_y,look_target_x,look_target_y," +
            "look_face_x,look_face_y,look_velocity_x,look_velocity_y," +
            "look_last_ms");
        for (String id : ids) output.write("," + csv(id));
        output.newLine();
        output.flush();
    }

    private static void row(BufferedWriter output, long timeNs, String kind,
        MouseRecord event, double[] look, Map<String, Float> values)
        throws IOException {
        double elapsed = (timeNs - START_NS) / 1_000_000.0;
        output.write(String.format(Locale.ROOT, "%.3f,%s", elapsed, kind));
        if (event == null) output.write(",-1,-1,\"\",0,0,0,0,0,0,0,0");
        else output.write(String.format(Locale.ROOT,
            ",%d,%d,%s,%d,%d,%d,%d,%d,%d,%d,%d", event.id, event.when,
            csv(event.source), event.bounds.x, event.bounds.y,
            event.bounds.width, event.bounds.height, event.x, event.y,
            event.screenX, event.screenY));
        for (double value : look)
            output.write(String.format(Locale.ROOT, ",%.9g", value));
        for (Float value : values.values())
            output.write(String.format(Locale.ROOT, ",%.9g", value));
        output.newLine();
    }

    private static void sample(Path path, boolean staticModel,
        boolean inspectUi, int expression) {
        try (BufferedWriter output = Files.newBufferedWriter(path,
            StandardCharsets.UTF_8)) {
            while (!findModel()) Thread.sleep(10);
            if (staticModel)
                while (!prepareStaticModel()) Thread.sleep(10);
            if (inspectUi) inspectTrees();
            if (expression >= 0) {
                boolean activated = false;
                for (int attempt = 0; attempt < 500 && !activated; ++attempt) {
                    activated = activateExpression(expression);
                    if (!activated) Thread.sleep(10);
                }
                if (!activated)
                    throw new IllegalStateException("Expression not found: " +
                        expression);
                Thread.sleep(1000);
            }
            LinkedHashMap<String, Float> previous = parameters();
            double[] previousLook = look();
            header(output, previous.keySet());
            row(output, System.nanoTime(), "ready", null, previousLook,
                previous);
            output.flush();
            ready = true;
            System.out.println("CUBISM_VIEWER_PROBE_READY parameters=" +
                previous.size());
            long lastFlush = System.nanoTime();
            for (;;) {
                LinkedHashMap<String, Float> current = parameters();
                double[] currentLook = look();
                MouseRecord event;
                while ((event = EVENTS.poll()) != null)
                    row(output, event.timeNs, "mouse", event, currentLook,
                        current);
                if (changed(previous, current) ||
                    changed(previousLook, currentLook)) {
                    row(output, System.nanoTime(), "sample", null,
                        currentLook, current);
                    previous = current;
                    previousLook = currentLook;
                }
                long now = System.nanoTime();
                if (now - lastFlush >= 20_000_000L) {
                    output.flush();
                    lastFlush = now;
                }
                Thread.sleep(1);
            }
        } catch (Throwable error) {
            error.printStackTrace();
            System.exit(3);
        }
    }

    private static void listenForMouse() {
        Toolkit.getDefaultToolkit().addAWTEventListener(event -> {
            if (ready && event instanceof MouseEvent)
                EVENTS.add(new MouseRecord((MouseEvent)event));
        }, AWTEvent.MOUSE_EVENT_MASK | AWTEvent.MOUSE_MOTION_EVENT_MASK);
    }

    public static void main(String[] args) throws Exception {
        if (args.length < 2) {
            System.err.println("usage: CubismViewerParameterProbe output.csv " +
                "model3.json [--static]");
            System.exit(2);
        }
        Path output = Paths.get(args[0]).toAbsolutePath();
        Files.createDirectories(output.getParent());
        listenForMouse();
        boolean staticModel = false, inspectUi = false;
        int expression = -1;
        for (int i = 2; i < args.length; ++i) {
            if ("--static".equals(args[i])) staticModel = true;
            else if ("--inspect-ui".equals(args[i])) inspectUi = true;
            else if (args[i].startsWith("--expression="))
                expression = Integer.parseInt(args[i].substring(13));
        }
        final boolean useStatic = staticModel, inspect = inspectUi;
        final int expressionIndex = expression;
        Thread sampler = new Thread(() -> sample(output, useStatic, inspect,
            expressionIndex),
            "cubism-viewer-parameter-probe");
        sampler.setDaemon(false);
        sampler.start();
        try {
            Class<?> viewer = Class.forName(VIEWER);
            Method main = viewer.getMethod("main", String[].class);
            main.invoke(null, (Object)new String[] {args[1]});
        } catch (InvocationTargetException error) {
            Throwable cause = error.getCause();
            if (cause instanceof Exception) throw (Exception)cause;
            if (cause instanceof Error) throw (Error)cause;
            throw error;
        }
    }
}
