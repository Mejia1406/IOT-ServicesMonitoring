import javax.swing.*;
import javax.swing.table.*;
import javax.swing.border.*;
import java.awt.*;
import java.awt.event.*;
import java.io.*;
import java.net.*;
import java.time.LocalTime;
import java.time.format.DateTimeFormatter;
import java.util.*;
import java.util.concurrent.*;

public class OperatorClient extends JFrame {

    static final Color BG        = new Color(13, 17, 23);
    static final Color BG2       = new Color(22, 27, 34);
    static final Color BG3       = new Color(33, 38, 45);
    static final Color FG        = new Color(230, 237, 243);
    static final Color GREEN     = new Color(63, 185, 80);
    static final Color BLUE      = new Color(88, 166, 255);
    static final Color ORANGE    = new Color(210, 153, 34);
    static final Color RED       = new Color(248, 81, 73);
    static final Color GRAY      = new Color(139, 148, 158);

    static String serverHost = System.getenv("IOT_SERVER_HOST") != null
            ? System.getenv("IOT_SERVER_HOST") : "localhost";
    static int serverPort = System.getenv("IOT_SERVER_PORT") != null
            ? Integer.parseInt(System.getenv("IOT_SERVER_PORT")) : 9000;

    private Socket           socket;
    private PrintWriter      writer;
    private BufferedReader   reader;
    private volatile boolean connected  = false;
    private volatile boolean running    = true;

    private final DefaultTableModel sensorModel;
    private final DefaultTableModel alertModel;
    private int msgCount   = 0;
    private int alertCount = 0;

    private JLabel  statusLabel;
    private JLabel  connLabel;
    private JLabel  statSensors;
    private JLabel  statAlerts;
    private JLabel  statMsgs;
    private JTextArea logArea;
    private JTextField cmdField;

    public OperatorClient() {
        super("IoT Monitoring — Panel de Operador");

        sensorModel = new DefaultTableModel(
                new String[]{"ID", "Tipo", "Valor", "Unidad", "Ubicación"}, 0) {
            @Override public boolean isCellEditable(int r, int c) { return false; }
        };
        alertModel = new DefaultTableModel(
                new String[]{"Hora", "Tipo", "Sensor", "Valor"}, 0) {
            @Override public boolean isCellEditable(int r, int c) { return false; }
        };

        buildUI();
        setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        addWindowListener(new WindowAdapter() {
            @Override public void windowClosing(WindowEvent e) {
                running = false;
                if (connected) sendMessage("QUIT|OP-001");
                if (socket != null) try { socket.close(); } catch (Exception ignored) {}
            }
        });

        setSize(1100, 700);
        setLocationRelativeTo(null);
        setVisible(true);

        SwingUtilities.invokeLater(this::showLogin);
    }

    private void buildUI() {
        getContentPane().setBackground(BG);
        setLayout(new BorderLayout(0, 0));

        add(buildHeader(),  BorderLayout.NORTH);
        add(buildCenter(),  BorderLayout.CENTER);
        add(buildCmdBar(),  BorderLayout.SOUTH);
    }

    private JPanel buildHeader() {
        JPanel header = new JPanel(new BorderLayout());
        header.setBackground(BG);
        header.setBorder(BorderFactory.createEmptyBorder(8, 12, 8, 12));

        JLabel title = new JLabel("📡 IoT MONITORING SYSTEM");
        title.setFont(new Font("Courier New", Font.BOLD, 16));
        title.setForeground(GREEN);
        header.add(title, BorderLayout.WEST);

        JPanel right = new JPanel(new FlowLayout(FlowLayout.RIGHT, 12, 0));
        right.setBackground(BG);

        connLabel  = styledLabel("", GRAY, 11);
        statusLabel = styledLabel("● DESCONECTADO", RED, 11);
        right.add(connLabel);
        right.add(statusLabel);
        header.add(right, BorderLayout.EAST);

        JPanel stats = new JPanel(new FlowLayout(FlowLayout.LEFT, 0, 0));
        stats.setBackground(BG2);
        stats.setBorder(BorderFactory.createEmptyBorder(6, 12, 6, 12));

        statSensors = statWidget(stats, "SENSORES", "0", GREEN);
        statAlerts  = statWidget(stats, "ALERTAS",  "0", RED);
        statMsgs    = statWidget(stats, "MENSAJES", "0", BLUE);

        JPanel wrap = new JPanel(new BorderLayout());
        wrap.setBackground(BG);
        wrap.add(header, BorderLayout.NORTH);
        wrap.add(stats,  BorderLayout.SOUTH);
        return wrap;
    }

    private JLabel statWidget(JPanel parent, String label, String val, Color color) {
        JPanel p = new JPanel();
        p.setLayout(new BoxLayout(p, BoxLayout.Y_AXIS));
        p.setBackground(BG2);
        p.setBorder(BorderFactory.createEmptyBorder(4, 20, 4, 20));

        JLabel num = new JLabel(val, SwingConstants.CENTER);
        num.setFont(new Font("Courier New", Font.BOLD, 24));
        num.setForeground(color);
        num.setAlignmentX(Component.CENTER_ALIGNMENT);

        JLabel lbl = new JLabel(label, SwingConstants.CENTER);
        lbl.setFont(new Font("Courier New", Font.PLAIN, 9));
        lbl.setForeground(GRAY);
        lbl.setAlignmentX(Component.CENTER_ALIGNMENT);

        p.add(num);
        p.add(lbl);
        parent.add(p);
        return num;
    }

    private JSplitPane buildCenter() {
        JSplitPane split = new JSplitPane(JSplitPane.HORIZONTAL_SPLIT,
                buildLeftPanel(), buildRightPanel());
        split.setDividerLocation(420);
        split.setBackground(BG);
        split.setBorder(BorderFactory.createEmptyBorder(4, 4, 0, 4));
        return split;
    }

    private JPanel buildLeftPanel() {
        JPanel p = new JPanel(new BorderLayout());
        p.setBackground(BG2);

        JLabel hdr = styledLabel(" SENSORES ACTIVOS", BLUE, 11);
        hdr.setOpaque(true);
        hdr.setBackground(BG3);
        hdr.setBorder(BorderFactory.createEmptyBorder(4, 6, 4, 6));
        p.add(hdr, BorderLayout.NORTH);

        JTable table = buildTable(sensorModel);
        table.setDefaultRenderer(Object.class, new SensorCellRenderer());
        JScrollPane scroll = darkScroll(table);
        p.add(scroll, BorderLayout.CENTER);

        JPanel btns = new JPanel(new FlowLayout(FlowLayout.LEFT, 6, 4));
        btns.setBackground(BG2);
        btns.add(darkButton("↺  Actualizar", GREEN, e -> {
            sendMessage("LIST_SENSORS");
            sendMessage("STATUS");
        }));
        btns.add(darkButton("⚠  Alertas", ORANGE, e -> showAlertsWindow()));
        p.add(btns, BorderLayout.SOUTH);
        return p;
    }

    private JPanel buildRightPanel() {
        JPanel p = new JPanel(new BorderLayout());
        p.setBackground(BG);

        JLabel hdr = styledLabel(" LOG EN TIEMPO REAL", GREEN, 11);
        hdr.setOpaque(true);
        hdr.setBackground(BG3);
        hdr.setBorder(BorderFactory.createEmptyBorder(4, 6, 4, 6));
        p.add(hdr, BorderLayout.NORTH);

        logArea = new JTextArea();
        logArea.setBackground(BG);
        logArea.setForeground(FG);
        logArea.setFont(new Font("Courier New", Font.PLAIN, 10));
        logArea.setEditable(false);
        logArea.setLineWrap(true);
        logArea.setWrapStyleWord(true);
        p.add(darkScroll(logArea), BorderLayout.CENTER);
        return p;
    }

    private JPanel buildCmdBar() {
        JPanel p = new JPanel(new BorderLayout(6, 0));
        p.setBackground(BG2);
        p.setBorder(BorderFactory.createEmptyBorder(4, 8, 4, 8));

        JLabel lbl = styledLabel("CMD:", GRAY, 10);
        p.add(lbl, BorderLayout.WEST);

        cmdField = new JTextField();
        cmdField.setBackground(BG3);
        cmdField.setForeground(FG);
        cmdField.setCaretColor(FG);
        cmdField.setFont(new Font("Courier New", Font.PLAIN, 10));
        cmdField.setBorder(BorderFactory.createEmptyBorder(4, 6, 4, 6));
        cmdField.addActionListener(e -> sendCmd());
        p.add(cmdField, BorderLayout.CENTER);

        p.add(darkButton("Enviar", GREEN, e -> sendCmd()), BorderLayout.EAST);
        return p;
    }

    private void showLogin() {
        JDialog dlg = new JDialog(this, "Iniciar sesión", true);
        dlg.getContentPane().setBackground(BG);
        dlg.setLayout(new BorderLayout());
        dlg.setSize(360, 310);
        dlg.setLocationRelativeTo(this);
        dlg.setResizable(false);

        JPanel top = new JPanel();
        top.setBackground(BG);
        top.setLayout(new BoxLayout(top, BoxLayout.Y_AXIS));
        top.setBorder(BorderFactory.createEmptyBorder(16, 0, 8, 0));

        JLabel t1 = new JLabel("IoT MONITORING", SwingConstants.CENTER);
        t1.setFont(new Font("Courier New", Font.BOLD, 16));
        t1.setForeground(GREEN);
        t1.setAlignmentX(Component.CENTER_ALIGNMENT);

        JLabel t2 = new JLabel("Panel de Operador", SwingConstants.CENTER);
        t2.setFont(new Font("Courier New", Font.PLAIN, 10));
        t2.setForeground(GRAY);
        t2.setAlignmentX(Component.CENTER_ALIGNMENT);

        top.add(t1);
        top.add(Box.createVerticalStrut(4));
        top.add(t2);
        dlg.add(top, BorderLayout.NORTH);

        JPanel form = new JPanel(new GridLayout(5, 2, 6, 8));
        form.setBackground(BG);
        form.setBorder(BorderFactory.createEmptyBorder(8, 24, 8, 24));

        JTextField fHost = loginField(serverHost);
        JTextField fPort = loginField(String.valueOf(serverPort));
        JTextField fOpId = loginField("OP-001");
        JTextField fUser = loginField("admin");
        JPasswordField fPass = new JPasswordField("admin");
        styleLoginField(fPass);

        form.add(loginLabel("Servidor:"));  form.add(fHost);
        form.add(loginLabel("Puerto:"));    form.add(fPort);
        form.add(loginLabel("Operador ID:")); form.add(fOpId);
        form.add(loginLabel("Usuario:"));   form.add(fUser);
        form.add(loginLabel("Contraseña:")); form.add(fPass);
        dlg.add(form, BorderLayout.CENTER);

        JPanel bottom = new JPanel();
        bottom.setBackground(BG);
        bottom.setLayout(new BoxLayout(bottom, BoxLayout.Y_AXIS));

        JLabel errLabel = new JLabel(" ", SwingConstants.CENTER);
        errLabel.setFont(new Font("Courier New", Font.PLAIN, 10));
        errLabel.setForeground(RED);
        errLabel.setAlignmentX(Component.CENTER_ALIGNMENT);

        JButton btnConnect = darkButton("  Conectar  ", GREEN, null);
        btnConnect.setAlignmentX(Component.CENTER_ALIGNMENT);
        btnConnect.addActionListener(e -> {
            String host  = fHost.getText().trim();
            String port  = fPort.getText().trim();
            String opid  = fOpId.getText().trim();
            String user  = fUser.getText().trim();
            String pass  = new String(fPass.getPassword()).trim();

            if (host.isEmpty() || opid.isEmpty() || user.isEmpty() || pass.isEmpty()) {
                errLabel.setText("Completa todos los campos");
                return;
            }

            int portNum;
            try { portNum = Integer.parseInt(port); }
            catch (NumberFormatException ex) { errLabel.setText("Puerto inválido"); return; }

            errLabel.setText("Conectando...");
            btnConnect.setEnabled(false);

            new Thread(() -> {
                try {
                    doConnect(host, portNum, opid, user, pass);
                    SwingUtilities.invokeLater(() -> {
                        dlg.dispose();
                        statusLabel.setText("● CONECTADO");
                        statusLabel.setForeground(GREEN);
                        connLabel.setText(host + ":" + portNum + "  ");
                        appendLog("Conectado como " + opid + " (" + user + ")", GREEN);
                        sendMessage("LIST_SENSORS");
                        sendMessage("LIST_ALERTS");
                        sendMessage("STATUS");
                    });
                } catch (Exception ex) {
                    SwingUtilities.invokeLater(() -> {
                        errLabel.setText(ex.getMessage());
                        btnConnect.setEnabled(true);
                    });
                }
            }).start();
        });

        bottom.add(errLabel);
        bottom.add(Box.createVerticalStrut(6));
        bottom.add(btnConnect);
        bottom.add(Box.createVerticalStrut(12));
        dlg.add(bottom, BorderLayout.SOUTH);

        dlg.setVisible(true);
    }

    private void doConnect(String host, int port,
                           String opid, String user, String pass) throws Exception {
        InetAddress addr = InetAddress.getByName(host);
        socket = new Socket();
        socket.connect(new InetSocketAddress(addr, port), 8000);
        socket.setSoTimeout(0);

        writer = new PrintWriter(new BufferedWriter(
                new OutputStreamWriter(socket.getOutputStream())), true);
        reader = new BufferedReader(
                new InputStreamReader(socket.getInputStream()));

        String authMsg = "AUTH_OPERATOR|" + opid + "|" + user + "|" + pass;
        writer.println(authMsg);

        socket.setSoTimeout(5000);
        String resp = reader.readLine();
        socket.setSoTimeout(0);

        if (resp == null || resp.startsWith("ERROR")) {
            socket.close();
            throw new Exception("Credenciales incorrectas: " + resp);
        }

        connected = true;

        Thread recvThread = new Thread(this::recvLoop);
        recvThread.setDaemon(true);
        recvThread.start();

        ScheduledExecutorService scheduler = Executors.newSingleThreadScheduledExecutor();
        scheduler.scheduleAtFixedRate(() -> {
            if (connected) {
                sendMessage("LIST_SENSORS");
                sendMessage("STATUS");
            }
        }, 5, 5, TimeUnit.SECONDS);
    }

    private void recvLoop() {
        try {
            String line;
            while (running && (line = reader.readLine()) != null) {
                final String msg = line.trim();
                if (!msg.isEmpty()) {
                    SwingUtilities.invokeLater(() -> handleMessage(msg));
                }
            }
        } catch (Exception e) {
            if (running) {
                SwingUtilities.invokeLater(() -> {
                    connected = false;
                    statusLabel.setText("● DESCONECTADO");
                    statusLabel.setForeground(RED);
                    appendLog("Conexión cerrada por el servidor", RED);
                });
            }
        }
    }

    private void handleMessage(String line) {
        msgCount++;
        statMsgs.setText(String.valueOf(msgCount));

        String[] parts = line.split("\\|");
        String cmd = parts[0];

        switch (cmd) {
            case "ALERT": {
                String type   = parts.length > 1 ? parts[1] : "?";
                String sensor = parts.length > 2 ? parts[2] : "?";
                String value  = parts.length > 3 ? parts[3] : "?";
                String ts     = LocalTime.now().format(DateTimeFormatter.ofPattern("HH:mm:ss"));

                alertModel.insertRow(0, new Object[]{ts, type, sensor, value});
                if (alertModel.getRowCount() > 50)
                    alertModel.removeRow(alertModel.getRowCount() - 1);

                alertCount++;
                statAlerts.setText(String.valueOf(alertCount));
                appendLog("⚠ ALERTA: " + type + " sensor=" + sensor + " valor=" + value, RED);
                break;
            }

            case "DATA": {
                if (parts.length >= 5) {
                    String sid  = parts[1];
                    String type = parts[2];
                    String val  = parts[3];
                    String unit = parts[4];
                    updateSensorRow(sid, type, val, unit, "");
                    appendLog("DATA: " + sid + " " + val + " " + unit, BLUE);
                }
                break;
            }

            case "STATUS": {
                for (int i = 1; i < parts.length; i++) {
                    if (parts[i].startsWith("sensors=")) {
                        statSensors.setText(parts[i].split("=")[1]);
                    }
                }
                appendLog("STATUS: " + String.join(" ", Arrays.copyOfRange(parts, 1, parts.length)), GRAY);
                break;
            }

            case "SENSORS": {
                sensorModel.setRowCount(0);
                for (int i = 1; i < parts.length; i++) {
                    String[] f = parts[i].split(",");
                    if (f.length >= 5) {
                        sensorModel.addRow(new Object[]{f[0], f[1], f[3], f[4], f[2]});
                    }
                }
                statSensors.setText(String.valueOf(sensorModel.getRowCount()));
                appendLog("Sensores actualizados: " + sensorModel.getRowCount(), GRAY);
                break;
            }

            case "ALERTS": {
                alertModel.setRowCount(0);
                for (int i = 1; i < parts.length; i++) {
                    String[] f = parts[i].split(",");
                    if (f.length >= 3) {
                        alertModel.addRow(new Object[]{"histórico", f[1], f[0], f[2]});
                    }
                }
                appendLog("Alertas recibidas: " + alertModel.getRowCount(), GRAY);
                break;
            }

            case "OK":
                appendLog("OK: " + String.join("|", Arrays.copyOfRange(parts, 1, parts.length)), GREEN);
                break;

            case "ERROR":
                appendLog("ERROR: " + String.join("|", Arrays.copyOfRange(parts, 1, parts.length)), RED);
                break;

            default:
                appendLog(line, GRAY);
        }
    }

    private void updateSensorRow(String sid, String type,
                                  String val, String unit, String loc) {
        for (int i = 0; i < sensorModel.getRowCount(); i++) {
            if (sid.equals(sensorModel.getValueAt(i, 0))) {
                sensorModel.setValueAt(val,  i, 2);
                sensorModel.setValueAt(unit, i, 3);
                return;
            }
        }
        sensorModel.addRow(new Object[]{sid, type, val, unit, loc});
        statSensors.setText(String.valueOf(sensorModel.getRowCount()));
    }

    private synchronized void sendMessage(String msg) {
        if (connected && writer != null) {
            writer.println(msg);
        }
    }

    private void sendCmd() {
        String cmd = cmdField.getText().trim();
        if (!cmd.isEmpty()) {
            sendMessage(cmd);
            cmdField.setText("");
            msgCount++;
            statMsgs.setText(String.valueOf(msgCount));
        }
    }

    private void appendLog(String msg, Color color) {
        String ts   = LocalTime.now().format(DateTimeFormatter.ofPattern("HH:mm:ss"));
        String line = "[" + ts + "] " + msg + "\n";
        logArea.append(line);
        String text = logArea.getText();
        String[] lines = text.split("\n");
        if (lines.length > 200) {
            logArea.setText(String.join("\n",
                    Arrays.copyOfRange(lines, lines.length - 200, lines.length)) + "\n");
        }
        logArea.setCaretPosition(logArea.getDocument().getLength());
    }

    private void showAlertsWindow() {
        JDialog dlg = new JDialog(this, "Alertas del Sistema", false);
        dlg.getContentPane().setBackground(BG);
        dlg.setLayout(new BorderLayout());
        dlg.setSize(700, 400);
        dlg.setLocationRelativeTo(this);

        JLabel hdr = styledLabel("  ⚠ ALERTAS RECIENTES", ORANGE, 11);
        hdr.setOpaque(true);
        hdr.setBackground(BG3);
        hdr.setBorder(BorderFactory.createEmptyBorder(6, 6, 6, 6));
        dlg.add(hdr, BorderLayout.NORTH);

        JTable alertTable = buildTable(alertModel);
        dlg.add(darkScroll(alertTable), BorderLayout.CENTER);

        sendMessage("LIST_ALERTS");
        dlg.setVisible(true);
    }

    private JLabel styledLabel(String text, Color color, int size) {
        JLabel l = new JLabel(text);
        l.setFont(new Font("Courier New", Font.PLAIN, size));
        l.setForeground(color);
        return l;
    }

    private JLabel loginLabel(String text) {
        JLabel l = styledLabel(text, FG, 10);
        l.setBackground(BG);
        l.setOpaque(true);
        return l;
    }

    private JTextField loginField(String val) {
        JTextField f = new JTextField(val);
        styleLoginField(f);
        return f;
    }

    private void styleLoginField(JTextField f) {
        f.setBackground(BG3);
        f.setForeground(FG);
        f.setCaretColor(FG);
        f.setFont(new Font("Courier New", Font.PLAIN, 10));
        f.setBorder(BorderFactory.createEmptyBorder(4, 6, 4, 6));
    }

    private JButton darkButton(String text, Color fg, ActionListener al) {
        JButton b = new JButton(text);
        b.setBackground(BG3);
        b.setForeground(fg);
        b.setFont(new Font("Courier New", Font.PLAIN, 10));
        b.setBorder(BorderFactory.createEmptyBorder(6, 12, 6, 12));
        b.setFocusPainted(false);
        b.setCursor(Cursor.getPredefinedCursor(Cursor.HAND_CURSOR));
        b.addMouseListener(new MouseAdapter() {
            public void mouseEntered(MouseEvent e) { b.setBackground(BG2); }
            public void mouseExited(MouseEvent e)  { b.setBackground(BG3); }
        });
        if (al != null) b.addActionListener(al);
        return b;
    }

    private JTable buildTable(DefaultTableModel model) {
        JTable t = new JTable(model);
        t.setBackground(BG2);
        t.setForeground(FG);
        t.setFont(new Font("Courier New", Font.PLAIN, 10));
        t.setRowHeight(22);
        t.setShowGrid(false);
        t.setIntercellSpacing(new Dimension(0, 0));
        t.getTableHeader().setBackground(BG3);
        t.getTableHeader().setForeground(BLUE);
        t.getTableHeader().setFont(new Font("Courier New", Font.PLAIN, 10));
        t.setSelectionBackground(BG3);
        t.setSelectionForeground(FG);
        return t;
    }

    private JScrollPane darkScroll(Component c) {
        JScrollPane sp = new JScrollPane(c);
        sp.setBackground(BG);
        sp.getViewport().setBackground(BG);
        sp.setBorder(BorderFactory.createEmptyBorder());
        sp.getVerticalScrollBar().setBackground(BG3);
        sp.getHorizontalScrollBar().setBackground(BG3);
        return sp;
    }

    class SensorCellRenderer extends DefaultTableCellRenderer {
        @Override
        public Component getTableCellRendererComponent(JTable table, Object value,
                boolean selected, boolean focus, int row, int col) {
            super.getTableCellRendererComponent(table, value, selected, focus, row, col);
            setBackground(selected ? BG3 : BG2);
            setForeground(FG);
            setFont(new Font("Courier New", Font.PLAIN, 10));
            setBorder(BorderFactory.createEmptyBorder(0, 6, 0, 6));

            if (col == 2) {
                try {
                    String type  = (String) table.getValueAt(row, 1);
                    double val   = Double.parseDouble(value.toString());
                    boolean warn = false;
                    switch (type) {
                        case "temperature": warn = val > 80 || val < -10; break;
                        case "vibration":   warn = val > 7.5; break;
                        case "energy":      warn = val > 800; break;
                        case "humidity":    warn = val > 90;  break;
                    }
                    if (warn) setForeground(RED);
                } catch (Exception ignored) {}
            }
            return this;
        }
    }

    public static void main(String[] args) {
        for (int i = 0; i < args.length - 1; i++) {
            if ("-host".equals(args[i])) serverHost = args[i + 1];
            if ("-port".equals(args[i])) serverPort = Integer.parseInt(args[i + 1]);
        }

        try {
            UIManager.setLookAndFeel(UIManager.getCrossPlatformLookAndFeelClassName());
        } catch (Exception ignored) {}

        SwingUtilities.invokeLater(OperatorClient::new);
    }
}