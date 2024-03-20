#include <WiFi.h> 
#include <ESPAsyncWebServer.h> 
#include <DNSServer.h> 
#include "FS.h" 
#include "SD_MMC.h" 
#include "sqlite3.h" 

const char* ssid = "ESP32-AP"; 
const char* password = "12345678"; 

const byte DNS_PORT = 53; 
IPAddress apIP(8, 8, 4, 4); // The IP address for the captive portal 
DNSServer dnsServer; 
AsyncWebServer server(80); 
sqlite3 *db; 

void setup() { 
    Serial.begin(115200); 

    // Initialize SD Card 
    if (!SD_MMC.begin()) { 
        Serial.println("Card Mount Failed"); 
        return; 
    } 
    Serial.println("SD Card Mounted successfully."); 

    // Check if database file exists, create it if not 
    if (!SD_MMC.exists("/task.db")) { 
        File dbFile = SD_MMC.open("/task.db", FILE_WRITE); 
        if (!dbFile) { 
            Serial.println("Failed to create database file"); 
            return; 
        } 
        dbFile.close(); 
        Serial.println("Database file created successfully."); 
    } else { 
        Serial.println("Database file already exists."); 
    } 

    // Open SQLite Database 
    int dbOpenResult = sqlite3_open("/sdcard/task.db", &db); 
    if (dbOpenResult != SQLITE_OK) { 
        Serial.printf("Failed to open database: %s\n", sqlite3_errmsg(db)); 
        return; 
    } 
    Serial.println("Database opened successfully."); 

    // Check if table exists or create it 
    char *errMsg; 
    const char *sql = "CREATE TABLE IF NOT EXISTS tasks (task TEXT);"; 
    if (sqlite3_exec(db, sql, 0, 0, &errMsg)) { 
        Serial.printf("Failed to create table: %s\n", errMsg); 
        sqlite3_free(errMsg); 
        return; 
    } 
    Serial.println("Table created successfully."); 

    // Set up WiFi in AP mode 
    WiFi.mode(WIFI_AP); 
    WiFi.softAP(ssid, password); 
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0)); 
    Serial.print("Access Point Started. IP Address: "); 
    Serial.println(WiFi.softAPIP()); 

    // Start DNS Server for Captive Portal 
    dnsServer.start(DNS_PORT, "*", apIP); 

    // Serve HTML file from SD Card 
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) { 
        request->send(SD_MMC, "/indexLegacy.html", "text/html"); 
    }); 
   // Serve Bootstrap.min.css file from the specified directory
 server.on("/bootstrap-5.3.3-dist/css/bootstrap.min.css", HTTP_GET, [](AsyncWebServerRequest *request) { 
        request->send(SD_MMC, "/bootstrap-5.3.3-dist/css/bootstrap.min.css", "text/css"); 
    }); 
    // Serve Bootstrap.bundle.min.js file from the specified directory
 server.on("/bootstrap-5.3.3-dist/js/bootstrap.bundle.min.js", HTTP_GET, [](AsyncWebServerRequest *request) { 
    request->send(SD_MMC, "/bootstrap-5.3.3-dist/js/bootstrap.bundle.min.js", "text/javascript"); 
});


    // Handle task saving via POST request 
    server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) { 
        if (request->hasParam("task", true)) { // Ensure parameter name matches your HTML form 
            String task = request->getParam("task", true)->value(); 
            char *errMsg; 
            String sql = "INSERT INTO tasks (task) VALUES ('" + task + "');"; 
            if (sqlite3_exec(db, sql.c_str(), 0, 0, &errMsg)) { 
                Serial.printf("Failed to insert task: %s\n", errMsg); 
                sqlite3_free(errMsg); 
                request->send(500, "text/plain", "Server Error"); 
                return; 
            } 
            request->send(200, "text/plain", "Task saved"); 
        } else { 
            request->send(400, "text/plain", "Bad Request"); 
        } 
    }); 

    // Handle fetching tasks via GET request 
    server.on("/tasks", HTTP_GET, [](AsyncWebServerRequest *request) { 
        String tasksHTML = ""; 
        sqlite3_stmt *stmt; 
        const char *sql = "SELECT task FROM tasks;"; 
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) { 
            tasksHTML += "<ul>"; // Start an unordered list 
            while (sqlite3_step(stmt) == SQLITE_ROW) { 
                String task = String((char*)sqlite3_column_text(stmt, 0)); 
                tasksHTML += "<li>" + task + " <button class='delete-btn' data-task='" + task + "'>Delete</button></li>"; 
            } 
            tasksHTML += "</ul>"; // End the unordered list 
            sqlite3_finalize(stmt); 
        } 
        request->send(200, "text/html", tasksHTML); 
    }); 

    // Handle task deletion via GET request 
    server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request) { 
        if (request->hasParam("task")) { 
            String taskToDelete = request->getParam("task")->value(); 
            char *errMsg; 
            String sql = "DELETE FROM tasks WHERE task = '" + taskToDelete + "';"; 
            if (sqlite3_exec(db, sql.c_str(), 0, 0, &errMsg) != SQLITE_OK) { 
                Serial.printf("Failed to delete task: %s\n", errMsg); 
                sqlite3_free(errMsg); 
                request->send(500, "text/plain", "Server Error"); 
                return; 
            } 
            // Respond with updated task list after deletion 
            String tasks = ""; 
            sqlite3_stmt *stmt; 
            const char *selectSql = "SELECT task FROM tasks;"; 
            if (sqlite3_prepare_v2(db, selectSql, -1, &stmt, NULL) == SQLITE_OK) { 
                while (sqlite3_step(stmt) == SQLITE_ROW) { 
                    tasks += String((char*)sqlite3_column_text(stmt, 0)) + "<br>"; 
                } 
                sqlite3_finalize(stmt); 
            } 
            request->send(200, "text/html", tasks); 
        } else { 
            request->send(400, "text/plain", "Bad Request"); 
        } 
    }); 
    // Handle task editing via POST request
server.on("/edit", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("oldTask", true) && request->hasParam("newTask", true)) {
        String oldTask = request->getParam("oldTask", true)->value();
        String newTask = request->getParam("newTask", true)->value();

        char *errMsg;
        String sql = "UPDATE tasks SET task = '" + newTask + "' WHERE task = '" + oldTask + "';";
        if (sqlite3_exec(db, sql.c_str(), 0, 0, &errMsg) != SQLITE_OK) {
            Serial.printf("Failed to edit task: %s\n", errMsg);
            sqlite3_free(errMsg);
            request->send(500, "text/plain", "Server Error");
            return;
        }

        // Respond with updated task list after editing
        String tasks = "";
        sqlite3_stmt *stmt;
        const char *selectSql = "SELECT task FROM tasks;";
        if (sqlite3_prepare_v2(db, selectSql, -1, &stmt, NULL) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                tasks += String((char*)sqlite3_column_text(stmt, 0)) + "<br>";
            }
            sqlite3_finalize(stmt);
        }
        request->send(200, "text/html", tasks);
    } else {
        request->send(400, "text/plain", "Bad Request");
    }
});
    // handle login via POST Request
   server.on("/login", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("username", true) && request->hasParam("password", true)) {
        String username = request->getParam("username", true)->value();
        String password = request->getParam("password", true)->value();

        // Check if the username and password exist in the users table
        sqlite3_stmt *stmt;
        const char *sql = "SELECT COUNT(*) FROM users WHERE username = ? AND password = ?";
        int result = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if (result != SQLITE_OK) {
            Serial.println("Failed to prepare statement");
            request->send(500, "text/plain", "Server Error");
            return;
        }

        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_TRANSIENT);

        result = sqlite3_step(stmt);
        int count = 0;
        if (result == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);

        if (count > 0) {
            // Successful login
            String response = "Welcome, " + username; // Construct the welcome message
            request->send(200, "text/plain", response); // Send the welcome message
            Serial.println("Login successful for user: " + username);
        } else {
            // Failed login
            request->send(401, "text/plain", "Login failed");
            Serial.println("Login failed for user: " + username);
        }
    } else {
        // Missing parameters
        request->send(400, "text/plain", "Bad Request");
    }
});

    // Handle user registration via POST request 
server.on("/register", HTTP_POST, [](AsyncWebServerRequest *request) { 
    if (request->hasParam("username", true) && request->hasParam("password", true)) { 
        String username = request->getParam("username", true)->value(); 
        String password = request->getParam("password", true)->value(); 

        // Check if the users table exists, if not, create it 
        char *errMsg; 
        const char *checkTableSQL = "SELECT name FROM sqlite_master WHERE type='table' AND name='users';"; 
        sqlite3_stmt *stmt; 
        if (sqlite3_prepare_v2(db, checkTableSQL, -1, &stmt, NULL) != SQLITE_OK) { 
            Serial.println("Failed to prepare statement for checking table existence"); 
            request->send(500, "text/plain", "Server Error"); 
            return; 
        } 
        if (sqlite3_step(stmt) != SQLITE_ROW) { // Table doesn't exist 
            const char *createTableSQL = "CREATE TABLE users (username TEXT PRIMARY KEY, password TEXT);"; 
            if (sqlite3_exec(db, createTableSQL, 0, 0, &errMsg) != SQLITE_OK) { 
                Serial.printf("Failed to create users table: %s\n", errMsg); 
                sqlite3_free(errMsg); 
                sqlite3_finalize(stmt); 
                request->send(500, "text/plain", "Server Error"); 
                return; 
            } 
            Serial.println("Users table created successfully."); 
        } 
        sqlite3_finalize(stmt); 

        // Insert the registration data into the users table 
        const char *insertUserSQL = "INSERT INTO users (username, password) VALUES (?, ?);"; 
        if (sqlite3_prepare_v2(db, insertUserSQL, -1, &stmt, NULL) != SQLITE_OK) { 
            Serial.println("Failed to prepare statement for user insertion"); 
            request->send(500, "text/plain", "Server Error"); 
            return; 
        } 
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT); 
        sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_TRANSIENT); 
        if (sqlite3_step(stmt) != SQLITE_DONE) { 
            Serial.println("Failed to insert user data into the table"); 
            request->send(500, "text/plain", "Server Error"); 
            return; 
        } 
        sqlite3_finalize(stmt); 

        Serial.println("Registration successful for user: " + username); // Serial message for successful registration
        request->send(200, "text/plain", "Registration successful"); 
    } else { 
        request->send(400, "text/plain", "Bad Request"); 
    } 
}); 
// Handle user logout via GET request
server.on("/logout", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Redirect the user back to the login page
    request->redirect("/");
});


    // Redirect all HTTP traffic to the captive portal 
    server.onNotFound([](AsyncWebServerRequest *request) { 
        request->redirect("/"); // Redirect to the root, which serves the captive portal page 
    }); 

    server.begin(); 
} 

void loop() { 
    dnsServer.processNextRequest(); // Process DNS queries to ensure captive portal functionality 
}