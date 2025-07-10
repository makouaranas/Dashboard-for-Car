package h;

import javafx.animation.FadeTransition;
import javafx.application.Application;
import javafx.application.Platform;
import javafx.geometry.Insets;
import javafx.geometry.Pos;
import javafx.scene.Scene;
import javafx.scene.control.Label;
import javafx.scene.control.ProgressBar;
import javafx.scene.effect.DropShadow;
import javafx.scene.effect.Glow;
import javafx.scene.effect.InnerShadow;
import javafx.scene.layout.*;
import javafx.scene.paint.Color;
import javafx.scene.shape.Circle;
import javafx.scene.text.Font;
import javafx.scene.text.FontWeight;
import javafx.stage.Stage;
import javafx.util.Duration;
import java.io.*;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

public class CarDashboard extends Application {
    // UI Components
    private Label speedLabel, rpmLabel, gearLabel, fuelLabel, tempLabel;
    private Label odoLabel, tripLabel, fuelRateLabel, dateTimeLabel, tempExtLabel;
    private ProgressBar fuelBar, tempBar;
    private FadeTransition leftSignalAnimation, rightSignalAnimation;
    private FadeTransition speedPulse, rpmPulse;
    private Circle batteryIndicator, lightsIndicator;
    private DatabaseManager dbManager;
    
    // CAN IDs
    private static final int CAN_ID_SPEED = 0x100;
    private static final int CAN_ID_RPM = 0x101;
    private static final int CAN_ID_FUEL_LEVEL = 0x102;
    private static final int CAN_ID_ENGINE_TEMP = 0x103;
    private static final int CAN_ID_TURN_LEFT = 0x104;
    private static final int CAN_ID_TURN_RIGHT = 0x105;
    private static final int CAN_ID_BATTERY = 0x106;
    private static final int CAN_ID_LIGHTS = 0x107;
    private static final int CAN_ID_GEAR = 0x108;
    private static final int CAN_ID_EXT_TEMP = 0x10E;
    private static final int CAN_ID_ODOMETER = 0x10A;
    private static final int CAN_ID_TRIP = 0x10B;
    private static final int CAN_ID_FUEL_RATE = 0x10C;
    private static final int CAN_ID_GEAR_POS = 0x10D;

    // State variables
    private volatile int speed = 0;
    private volatile int rpm = 0;
    private volatile int fuelLevel = 79;
    private volatile int engineTemp = 90;
    private volatile boolean turnLeft = false;
    private volatile boolean turnRight = false;
    private volatile boolean batteryOk = true;
    private volatile boolean lightsOn = false;
    private volatile int extTemp = 19;
    private volatile double odometer = 397.0;
    private volatile double tripDistance = 79.0;
    private volatile double fuelRate = 5.2;
    private volatile char transmissionMode = 'D';
    private volatile int currentGear = 1;

    // Executor and process
    private ScheduledExecutorService executor;
    private Process candumpProcess;

    public static void main(String[] args) {
        launch(args);
    }

    @Override
    public void start(Stage primaryStage) {
        dbManager = new DatabaseManager();
        primaryStage.setTitle("Vehicle Dashboard - CAN Monitor");
        
        executor = Executors.newScheduledThreadPool(2);
        
        BorderPane root = new BorderPane();
        root.setPadding(new Insets(10));
        root.setStyle("-fx-background-color: #121212;");
        
        root.setTop(createTopPanel());
        root.setCenter(createCenterPanel());
        root.setBottom(createBottomPanel());
        
        Scene scene = new Scene(root, 1000, 620);
        primaryStage.setScene(scene);
        primaryStage.show();
        
        startClock();
        startCANReader();
        
        primaryStage.setOnCloseRequest(e -> {
            stop();
            Platform.exit();
        });
    }

    private HBox createTopPanel() {
        HBox topPanel = new HBox(20);
        topPanel.setPadding(new Insets(10));
        topPanel.setAlignment(Pos.CENTER);

        batteryIndicator = createIndicatorCircle();
        VBox batteryBox = new VBox(5);
        batteryBox.setAlignment(Pos.CENTER);
        Label batteryLabel = new Label("BATTERY");
        batteryLabel.setTextFill(Color.LIGHTGRAY);
        batteryLabel.setFont(Font.font(12));
        batteryBox.getChildren().addAll(batteryLabel, batteryIndicator);

        lightsIndicator = createIndicatorCircle();
        VBox lightsBox = new VBox(5);
        lightsBox.setAlignment(Pos.CENTER);
        Label lightsLabel = new Label("LIGHTS");
        lightsLabel.setTextFill(Color.LIGHTGRAY);
        lightsLabel.setFont(Font.font(12));
        lightsBox.getChildren().addAll(lightsLabel, lightsIndicator);

        VBox centerBox = new VBox(5);
        centerBox.setAlignment(Pos.CENTER);
        
        dateTimeLabel = new Label();
        dateTimeLabel.setFont(Font.font("Arial", FontWeight.BOLD, 20));
        dateTimeLabel.setTextFill(Color.WHITE);
        
        tempExtLabel = new Label("19.5 °C");
        tempExtLabel.setFont(Font.font("Arial", FontWeight.BOLD, 24));
        tempExtLabel.setTextFill(Color.WHITE);
        
        centerBox.getChildren().addAll(dateTimeLabel, tempExtLabel);

        Region leftSpacer = new Region();
        Region rightSpacer = new Region();
        HBox.setHgrow(leftSpacer, Priority.ALWAYS);
        HBox.setHgrow(rightSpacer, Priority.ALWAYS);

        topPanel.getChildren().addAll(leftSpacer, batteryBox, centerBox, lightsBox, rightSpacer);
        return topPanel;
    }

    private Circle createIndicatorCircle() {
        Circle circle = new Circle(15);
        circle.setFill(Color.GRAY);
        circle.setStroke(Color.LIGHTGRAY);
        circle.setStrokeWidth(2);
        return circle;
    }

    private GridPane createCenterPanel() {
        GridPane grid = new GridPane();
        grid.setAlignment(Pos.CENTER);
        grid.setHgap(40);
        grid.setVgap(30);
        grid.setPadding(new Insets(20));

        // Speed Gauge
        StackPane speedGauge = createCircularGauge("SPEED (km/h)", "0", Color.web("#0077FF"), 64);
        grid.add(speedGauge, 0, 0);

        // RPM Gauge
        StackPane rpmGauge = createCircularGauge("RPM", "0", Color.web("#AA00FF"), 64);
        grid.add(rpmGauge, 2, 0);

        // Gear Display
        VBox gearBox = new VBox(10);
        gearBox.setAlignment(Pos.CENTER);
        gearBox.setStyle("-fx-border-color: black; -fx-border-width: 2; -fx-border-radius: 10; " +
                       "-fx-padding: 20; -fx-background-color: rgba(30,30,30,0.7);");
        gearLabel = createValueLabel("D");
        gearLabel.setFont(Font.font("Arial", FontWeight.BOLD, 72));
        gearLabel.setTextFill(Color.WHITE);
        Label gearTitle = new Label("GEAR");
        gearTitle.setTextFill(Color.LIGHTGRAY);
        gearTitle.setFont(Font.font(20));
        gearBox.getChildren().addAll(gearTitle, gearLabel);
        grid.add(gearBox, 1, 0);

        // Bottom indicators
        HBox bottomBox = new HBox(30);
        bottomBox.setAlignment(Pos.CENTER);

        StackPane leftArrow = createBlinkingArrow(true);
        VBox tempBox = createProgressBox("TEMP", "#e74c3c");
        VBox fuelBox = createProgressBox("FUEL", "#3498db");
        StackPane rightArrow = createBlinkingArrow(false);

        bottomBox.getChildren().addAll(leftArrow, tempBox, fuelBox, rightArrow);
        grid.add(bottomBox, 0, 1, 3, 1);

        return grid;
    }

    private StackPane createCircularGauge(String title, String value, Color color, double fontSize) {
        StackPane gauge = new StackPane();
        gauge.setMinSize(250, 250);
        
        Color innerGlowColor = title.equals("SPEED (km/h)") ? Color.web("#00AAFF") : Color.web("#DD00FF");
        
        DropShadow outerGlow = new DropShadow();
        outerGlow.setColor(color);
        outerGlow.setRadius(25);
        outerGlow.setSpread(0.7);
        
        InnerShadow innerGlow = new InnerShadow();
        innerGlow.setColor(innerGlowColor);
        innerGlow.setRadius(15);
        innerGlow.setChoke(0.7);
        
        outerGlow.setInput(innerGlow);
        
        Circle outerCircle = new Circle(120, Color.TRANSPARENT);
        outerCircle.setStroke(color);
        outerCircle.setStrokeWidth(6);
        outerCircle.setEffect(outerGlow);
        
        Glow textGlow = new Glow(0.9);
        
        Label valueLabel = new Label(value);
        valueLabel.setFont(Font.font("Arial", FontWeight.BOLD, fontSize));
        valueLabel.setTextFill(color);
        valueLabel.setEffect(textGlow);
        
        Label titleLabel = new Label(title);
        titleLabel.setFont(Font.font(16));
        titleLabel.setTextFill(Color.LIGHTGRAY);
        
        VBox content = new VBox(5, valueLabel, titleLabel);
        content.setAlignment(Pos.CENTER);
        
        Circle background = new Circle(120, Color.rgb(20, 20, 20, 0.8));
        background.setStroke(Color.rgb(80, 80, 80));
        background.setStrokeWidth(3);
        
        gauge.getChildren().addAll(background, outerCircle, content);
        
        FadeTransition pulse = new FadeTransition(Duration.seconds(2), outerCircle);
        pulse.setFromValue(0.6);
        pulse.setToValue(1.0);
        pulse.setCycleCount(FadeTransition.INDEFINITE);
        pulse.setAutoReverse(true);
        
        if (title.equals("SPEED (km/h)")) {
            speedLabel = valueLabel;
            speedPulse = pulse;
            speedPulse.play();
        } else if (title.equals("RPM")) {
            rpmLabel = valueLabel;
            rpmPulse = pulse;
            rpmPulse.play();
        }
        
        return gauge;
    }

    private VBox createProgressBox(String title, String color) {
        VBox box = new VBox(10);
        box.setAlignment(Pos.CENTER);
        box.setPadding(new Insets(15));
        box.setStyle("-fx-background-color: rgba(30,30,30,0.7); -fx-background-radius: 10;");

        Label valueLabel = new Label("0");
        valueLabel.setFont(Font.font("Arial", FontWeight.BOLD, 24));
        valueLabel.setTextFill(Color.WHITE);

        ProgressBar progressBar = new ProgressBar();
        progressBar.setPrefWidth(200);
        progressBar.setPrefHeight(20);
        progressBar.setStyle("-fx-accent: " + color + "; -fx-background-color: #333333;");

        Label titleLabel = new Label(title);
        titleLabel.setTextFill(Color.LIGHTGRAY);
        titleLabel.setFont(Font.font(16));

        box.getChildren().addAll(titleLabel, valueLabel, progressBar);

        if (title.equals("FUEL")) {
            fuelLabel = valueLabel;
            fuelBar = progressBar;
            fuelLabel.setText(fuelLevel + "%");
            fuelBar.setProgress(fuelLevel / 100.0);
        } else if (title.equals("TEMP")) {
            tempLabel = valueLabel;
            tempBar = progressBar;
            tempLabel.setText(engineTemp + "°C");
            tempBar.setProgress(engineTemp / 120.0);
        }

        return box;
    }

    private GridPane createBottomPanel() {
        GridPane grid = new GridPane();
        grid.setHgap(30);
        grid.setVgap(15);
        grid.setPadding(new Insets(20));
        grid.setAlignment(Pos.CENTER);
        grid.setStyle("-fx-border-color: #34495e; -fx-border-width: 1; -fx-border-radius: 5; " +
                     "-fx-padding: 15; -fx-background-color: rgba(30,30,30,0.7);");
        
        odoLabel = createValueLabel(String.format("%.1f km", odometer));
        Label odoTitle = new Label("ODOMETER:");
        odoTitle.setTextFill(Color.LIGHTGRAY);
        odoTitle.setFont(Font.font(14));
        grid.add(odoTitle, 0, 0);
        grid.add(odoLabel, 1, 0);
        
        tripLabel = createValueLabel(String.format("%.1f km", tripDistance));
        Label tripTitle = new Label("TRIP:");
        tripTitle.setTextFill(Color.LIGHTGRAY);
        tripTitle.setFont(Font.font(14));
        grid.add(tripTitle, 2, 0);
        grid.add(tripLabel, 3, 0);
        
        fuelRateLabel = createValueLabel(String.format("%.1f L/100km", fuelRate));
        Label fuelRateTitle = new Label("FUEL RATE:");
        fuelRateTitle.setTextFill(Color.LIGHTGRAY);
        fuelRateTitle.setFont(Font.font(14));
        grid.add(fuelRateTitle, 0, 1);
        grid.add(fuelRateLabel, 1, 1);
        
        return grid;
    }

    private StackPane createBlinkingArrow(boolean isLeft) {
        StackPane pane = new StackPane();
        pane.setMinSize(40, 40);

        Label arrowLabel = new Label(isLeft ? "←" : "→");
        arrowLabel.setFont(Font.font("Arial", FontWeight.BOLD, 36));
        arrowLabel.setTextFill(Color.LIMEGREEN);

        FadeTransition blink = new FadeTransition(Duration.millis(500), arrowLabel);
        blink.setFromValue(1.0);
        blink.setToValue(0.2);
        blink.setCycleCount(FadeTransition.INDEFINITE);
        blink.setAutoReverse(true);

        if (isLeft) {
            leftSignalAnimation = blink;
        } else {
            rightSignalAnimation = blink;
        }

        pane.getChildren().add(arrowLabel);
        return pane;
    }

    private Label createValueLabel(String text) {
        Label label = new Label(text);
        label.setFont(Font.font("Arial", FontWeight.BOLD, 18));
        label.setTextFill(Color.WHITE);
        return label;
    }

    private void startClock() {
        executor.scheduleAtFixedRate(() -> {
            Platform.runLater(() -> {
                DateTimeFormatter dtf = DateTimeFormatter.ofPattern("dd / MM    HH:mm");
                dateTimeLabel.setText(dtf.format(LocalDateTime.now()));
            });
        }, 0, 1, TimeUnit.MINUTES);
    }

    private void startCANReader() {
        executor.execute(() -> {
            try {
                System.out.println("Starting CAN reader...");
                setupCANInterface();
                
                ProcessBuilder pb = new ProcessBuilder("candump", "vcan0");
                candumpProcess = pb.start();
                
                BufferedReader reader = new BufferedReader(new InputStreamReader(candumpProcess.getInputStream()));
                
                String line;
                while ((line = reader.readLine()) != null && !Thread.currentThread().isInterrupted()) {
                    try {
                        parseCandumpLine(line);
                    } catch (Exception e) {
                        System.err.println("Error parsing CAN line: " + line);
                        e.printStackTrace();
                    }
                }
            } catch (IOException e) {
                System.err.println("Failed to start CAN reader: " + e.getMessage());
            }
        });
    }

    private void setupCANInterface() {
        try {
            new ProcessBuilder("sudo", "modprobe", "vcan").start().waitFor();
            new ProcessBuilder("sudo", "ip", "link", "add", "dev", "vcan0", "type", "vcan").start().waitFor();
            new ProcessBuilder("sudo", "ip", "link", "set", "up", "vcan0").start().waitFor();
            System.out.println("CAN interface vcan0 setup complete");
        } catch (Exception e) {
            System.err.println("Failed to setup CAN interface: " + e.getMessage());
        }
    }

    private void parseCandumpLine(String line) {
        try {
            String[] parts = line.trim().split("\\s+");
            if (parts.length < 4) return;
            
            String canIdStr = parts[1];
            int canId = Integer.parseInt(canIdStr, 16);
            
            String lengthStr = parts[2];
            if (!lengthStr.startsWith("[") || !lengthStr.endsWith("]")) return;
            int dataLength = Integer.parseInt(lengthStr.substring(1, lengthStr.length() - 1));
            
            byte[] data = new byte[dataLength];
            for (int i = 0; i < dataLength && i + 3 < parts.length; i++) {
                data[i] = (byte) Integer.parseInt(parts[i + 3], 16);
            }
            
            Platform.runLater(() -> processCANFrame(canId, data));
            
        } catch (Exception e) {
            System.err.println("Error parsing candump line: " + line);
            e.printStackTrace();
        }
    }

    private void processCANFrame(int canId, byte[] data) {
        switch (canId) {
            case CAN_ID_SPEED:
                if (data.length >= 2) {
                    speed = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN).getShort() & 0xFFFF;
                    updateUI();
                }
                break;
                
            case CAN_ID_RPM:
                if (data.length >= 2) {
                    rpm = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN).getShort() & 0xFFFF;
                    updateUI();
                }
                break;
                
            case CAN_ID_FUEL_LEVEL:
                if (data.length >= 1) {
                    fuelLevel = data[0] & 0xFF;
                    updateUI();
                }
                break;
                
            case CAN_ID_ENGINE_TEMP:
                if (data.length >= 1) {
                    engineTemp = data[0] & 0xFF;
                    updateUI();
                }
                break;
                
            case CAN_ID_TURN_LEFT:
                if (data.length >= 1) {
                    turnLeft = data[0] != 0;
                    updateUI();
                }
                break;
                
            case CAN_ID_TURN_RIGHT:
                if (data.length >= 1) {
                    turnRight = data[0] != 0;
                    updateUI();
                }
                break;
                
            case CAN_ID_BATTERY:
                if (data.length >= 1) {
                    batteryOk = data[0] != 0;
                    updateUI();
                }
                break;
                
            case CAN_ID_LIGHTS:
                if (data.length >= 1) {
                    lightsOn = data[0] != 0;
                    updateUI();
                }
                break;
                
            case CAN_ID_GEAR:
                if (data.length >= 1) {
                    char newMode = (char) data[0];
                    if (newMode == 'P' || newMode == 'R' || newMode == 'N' || newMode == 'D') {
                        transmissionMode = newMode;
                        updateUI();
                    }
                }
                break;
                
            case CAN_ID_EXT_TEMP:
                if (data.length >= 1) {
                    extTemp = data[0] & 0xFF;
                    Platform.runLater(() -> tempExtLabel.setText(extTemp + " °C"));
                }
                break;
                
            case CAN_ID_ODOMETER:
                if (data.length >= 4) {
                    odometer = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN).getInt() / 10.0;
                    updateUI();
                }
                break;
                
            case CAN_ID_TRIP:
                if (data.length >= 2) {
                    tripDistance = (ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN).getShort() & 0xFFFF) / 10.0;
                    updateUI();
                }
                break;
                
            case CAN_ID_FUEL_RATE:
                if (data.length >= 2) {
                    fuelRate = (ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN).getShort() & 0xFFFF) / 10.0;
                    updateUI();
                }
                break;
                
            case CAN_ID_GEAR_POS:
                if (data.length >= 1) {
                    currentGear = data[0] & 0xFF;
                    updateUI();
                }
                break;
        }
    }

    private String getFormattedGearDisplay() {
        if (transmissionMode == 'D') {
            return "D" + currentGear;
        }
        return String.valueOf(transmissionMode);
    }

    private void updateUI() {
        dbManager.saveData(
            speed, rpm, fuelLevel, engineTemp,
            turnLeft, turnRight, batteryOk,
            lightsOn, extTemp, odometer,
            tripDistance, fuelRate, transmissionMode
        );
        
        if (speedLabel != null) speedLabel.setText(String.valueOf(speed));
        if (rpmLabel != null) rpmLabel.setText(String.valueOf(rpm));
        
        if (gearLabel != null) {
            gearLabel.setText(getFormattedGearDisplay());
        }
        
        if (fuelLabel != null) fuelLabel.setText(fuelLevel + "%");
        if (fuelBar != null) {
            fuelBar.setProgress(Math.max(0, Math.min(1.0, fuelLevel / 100.0)));
            if (fuelLevel < 20) {
                fuelBar.setStyle("-fx-accent: #e74c3c; -fx-background-color: #333333;");
            } else if (fuelLevel < 50) {
                fuelBar.setStyle("-fx-accent: #f39c12; -fx-background-color: #333333;");
            } else {
                fuelBar.setStyle("-fx-accent: #2ecc71; -fx-background-color: #333333;");
            }
        }
        
        if (tempLabel != null) tempLabel.setText(engineTemp + "°C");
        if (tempBar != null) {
            tempBar.setProgress(Math.max(0, Math.min(1.0, engineTemp / 120.0)));
            if (engineTemp > 90) {
                tempBar.setStyle("-fx-accent: #e74c3c; -fx-background-color: #333333;");
            } else if (engineTemp > 70) {
                tempBar.setStyle("-fx-accent: #f39c12; -fx-background-color: #333333;");
            } else {
                tempBar.setStyle("-fx-accent: #3498db; -fx-background-color: #333333;");
            }
        }
        
        if (odoLabel != null) odoLabel.setText(String.format("%.1f km", odometer));
        if (tripLabel != null) tripLabel.setText(String.format("%.1f km", tripDistance));
        if (fuelRateLabel != null) fuelRateLabel.setText(String.format("%.1f L/100km", fuelRate));
        
        if (batteryIndicator != null) {
            batteryIndicator.setFill(batteryOk ? Color.LIGHTGREEN : Color.RED);
        }
        
        if (lightsIndicator != null) {
            lightsIndicator.setFill(lightsOn ? Color.YELLOW : Color.GRAY);
        }
        
        if (leftSignalAnimation != null) {
            if (turnLeft) {
                leftSignalAnimation.play();
                leftSignalAnimation.getNode().setOpacity(1.0);
            } else {
                leftSignalAnimation.stop();
                leftSignalAnimation.getNode().setOpacity(0.2);
            }
        }

        if (rightSignalAnimation != null) {
            if (turnRight) {
                rightSignalAnimation.play();
                rightSignalAnimation.getNode().setOpacity(1.0);
            } else {
                rightSignalAnimation.stop();
                rightSignalAnimation.getNode().setOpacity(0.2);
            }
        }
    }

    @Override
    public void stop() {
        if (dbManager != null) {
            dbManager.close();
        }
        
        System.out.println("Shutting down dashboard...");
        
        if (speedPulse != null) speedPulse.stop();
        if (rpmPulse != null) rpmPulse.stop();
        
        if (candumpProcess != null && candumpProcess.isAlive()) {
            candumpProcess.destroyForcibly();
        }
        
        if (executor != null) {
            executor.shutdownNow();
        }
    }
}