package h;

import java.sql.*;
import java.io.File;

public class DatabaseManager {
    private static final String DB_URL = "jdbc:sqlite:vehicle_dashboard.db";
    private Connection connection;
    private long lastSaveTime = 0;

    public DatabaseManager() {
        try {
            // Créer le fichier de base de données s'il n'existe pas
            File dbFile = new File("vehicle_dashboard.db");
            if (!dbFile.exists()) {
                dbFile.createNewFile();
                System.out.println("Fichier DB créé : " + dbFile.getAbsolutePath());
            }
            
            // Ouvrir la connexion
            connection = DriverManager.getConnection(DB_URL);
            createTables();
        } catch (Exception e) {
            System.err.println("ERREUR DB: " + e.getMessage());
            e.printStackTrace();
        }
    }

    private void createTables() throws SQLException {
        String sqlDashboard = "CREATE TABLE IF NOT EXISTS dashboard_data (" +
                "id INTEGER PRIMARY KEY AUTOINCREMENT," +
                "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP," +
                "speed INTEGER, rpm INTEGER, fuel_level INTEGER," +
                "engine_temp INTEGER, turn_left BOOLEAN, turn_right BOOLEAN," +
                "battery_ok BOOLEAN, lights_on BOOLEAN, ext_temp INTEGER," +
                "odometer REAL, trip_distance REAL, fuel_rate REAL," +
                "transmission_mode CHAR(1))";
                
        String sqlAlerts = "CREATE TABLE IF NOT EXISTS alerts (" +
                "id INTEGER PRIMARY KEY AUTOINCREMENT," +
                "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP," +
                "alert_type TEXT, alert_message TEXT, is_active BOOLEAN)";

        try (Statement stmt = connection.createStatement()) {
            stmt.execute(sqlDashboard);
            stmt.execute(sqlAlerts);
            System.out.println("Tables créées avec succès");
        }
    }

    public synchronized void saveData(int speed, int rpm, int fuelLevel, int engineTemp,
                                    boolean turnLeft, boolean turnRight, boolean batteryOk,
                                    boolean lightsOn, int extTemp, double odometer,
                                    double tripDistance, double fuelRate, char transmissionMode) {
        long now = System.currentTimeMillis();
        if (now - lastSaveTime < 1000) { // 1 seconde entre les enregistrements
            return;
        }
        lastSaveTime = now;

        String sql = "INSERT INTO dashboard_data(speed, rpm, fuel_level, engine_temp, " +
                   "turn_left, turn_right, battery_ok, lights_on, ext_temp, odometer, " +
                   "trip_distance, fuel_rate, transmission_mode) " +
                   "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)";
        
        try (PreparedStatement pstmt = connection.prepareStatement(sql)) {
            pstmt.setInt(1, speed);
            pstmt.setInt(2, rpm);
            pstmt.setInt(3, fuelLevel);
            pstmt.setInt(4, engineTemp);
            pstmt.setBoolean(5, turnLeft);
            pstmt.setBoolean(6, turnRight);
            pstmt.setBoolean(7, batteryOk);
            pstmt.setBoolean(8, lightsOn);
            pstmt.setInt(9, extTemp);
            pstmt.setDouble(10, odometer);
            pstmt.setDouble(11, tripDistance);
            pstmt.setDouble(12, fuelRate);
            pstmt.setString(13, String.valueOf(transmissionMode));
            
            pstmt.executeUpdate();
            System.out.println("Données sauvegardées à " + new java.util.Date());
        } catch (SQLException e) {
            System.err.println("Erreur sauvegarde: " + e.getMessage());
        }
    }

    public void close() {
        try {
            if (connection != null && !connection.isClosed()) {
                connection.close();
                System.out.println("Connexion DB fermée");
            }
        } catch (SQLException e) {
            System.err.println("Erreur fermeture DB: " + e.getMessage());
        }
    }
}