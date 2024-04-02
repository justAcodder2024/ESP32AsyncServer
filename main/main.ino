#include <WiFi.h> 
#include <ESPAsyncWebServer.h> 
#include <DNSServer.h> 
#include "FS.h" 
#include "SD_MMC.h" 
#include "sqlite3.h" 
#include <map>

const char* ssid = "ESP32-AP"; 
const char* password = "12345678"; 

const byte DNS_PORT = 53; 
IPAddress apIP(8, 8, 4, 4); // The IP address for the captive portal 
DNSServer dnsServer; 
AsyncWebServer server(80); 
sqlite3 *db; 

struct Session {
    String sessionId;
    String username;
    int userId; // Add user ID field
};

std::map<String, Session> sessions;

String generateSessionId() {
    // Generate a random session ID
    String sessionId = "";
    char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < 20; ++i) {
        sessionId += charset[random(0, sizeof(charset) - 1)];
    }
    return sessionId;
}

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
    const char *sqlUsers = "CREATE TABLE IF NOT EXISTS users (user_id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT, password TEXT)";
      char *errMsg; 
    if (sqlite3_exec(db, sqlUsers, 0, 0, &errMsg)) { 
        Serial.printf("Failed to create table: %s\n", errMsg); 
        sqlite3_free(errMsg); 
        return; 
    } 
    Serial.println("Table created successfully."); 



    // Check if table exists or create it 
    const char *sqlTasks = "CREATE TABLE IF NOT EXISTS tasks (task_id INTEGER PRIMARY KEY AUTOINCREMENT,tasks TEXT,user_id INTEGER,FOREIGN KEY (user_id) REFERENCES users(user_id) );"; 
    char *errMsg1; 
    if (sqlite3_exec(db, sqlTasks, 0, 0, &errMsg1)) { 
        Serial.printf("Failed to create table: %s\n", errMsg1); 
        sqlite3_free(errMsg1); 
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
        // Serve Bootstrap.bundle.min.js file from the specified directory
 server.on("/three.min.js", HTTP_GET, [](AsyncWebServerRequest *request) { 
    request->send(SD_MMC, "/three.min.js", "text/javascript"); 
});
    
    // Handle login via POST Request
server.on("/login", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("username", true) && request->hasParam("password", true)) {
        String username = request->getParam("username", true)->value();
        String password = request->getParam("password", true)->value();

        // Check if the username and password exist in the users table
        sqlite3_stmt *stmt;
        const char *sql = "SELECT user_id FROM users WHERE username = ? AND password = ?";
        int result = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if (result != SQLITE_OK) {
            Serial.println("Failed to prepare statement");
            request->send(500, "text/plain", "Server Error");
            return;
        }

        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_TRANSIENT);

        result = sqlite3_step(stmt);
        int userId = -1; // Initialize user ID to -1 (invalid)
        if (result == SQLITE_ROW) {
            userId = sqlite3_column_int(stmt, 0); // Get the user ID from the query result
        }
        sqlite3_finalize(stmt);

        if (userId != -1) {
            // Successful login
            String sessionId = generateSessionId(); // Generate session ID
            Session session;
            session.sessionId = sessionId;
            session.username = username;
            session.userId = userId; // Store user ID in the session

            sessions[sessionId] = session; // Store session in the sessions map

            // Respond with session ID in the headers
            AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "Login successful "+username);
            response->addHeader("sessionId", sessionId);
            request->send(response);

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

        // Declare the stmt variable
        sqlite3_stmt *stmt = nullptr;

        // Insert the registration data into the users table 
        const char *insertUserSQL = "INSERT INTO users (username, password) VALUES (?, ?);";
        if (sqlite3_prepare_v2(db, insertUserSQL, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_DONE) {
                request->send(200, "text/plain", "Registration successful");
            } else {
                request->send(500, "text/plain", "Server Error");
            }
            sqlite3_finalize(stmt);
        } else {
            request->send(500, "text/plain", "Failed to prepare user insertion");
        }
    }
});
// Handle saving tasks via POST request
server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Check if the request has the "sessionId" header
    if (!request->hasHeader("sessionId")) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }

    // Extract the session ID from the request headers
    String sessionId = request->getHeader("sessionId")->value();

    // Validate session ID
    if (sessions.find(sessionId) == sessions.end()) {
        request->send(401, "text/plain", "Invalid session ID");
        return;
    }

    // Get the user ID associated with the session ID
    String userId = String(sessions[sessionId].userId);

    // Check if the request contains the "task" parameter
    if (!request->hasParam("task", true)) {
        request->send(400, "text/plain", "Bad Request: Missing 'task' parameter");
        return;
    }

    // Extract the task from the request parameters
    String task = request->getParam("task", true)->value();

    // Prepare the SQL statement to insert the task into the tasks table
    String sql = "INSERT INTO tasks (tasks, user_id) VALUES (?, ?);";
    sqlite3_stmt *stmt;
    int result = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL);
    if (result != SQLITE_OK) {
        Serial.println("Failed to prepare SQL statement: " + String(sqlite3_errmsg(db)));
        request->send(500, "text/plain", "Server Error: Failed to prepare SQL statement");
        return;
    }

    // Bind parameters to the SQL statement
    sqlite3_bind_text(stmt, 1, task.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, userId.c_str(), -1, SQLITE_TRANSIENT);

    // Execute the SQL statement
    result = sqlite3_step(stmt);
    if (result != SQLITE_DONE) {
        Serial.println("Failed to insert task into database: " + String(sqlite3_errmsg(db)));
        request->send(500, "text/plain", "Server Error: Failed to insert task into database");
        return;
    }

    // Finalize the SQL statement
    sqlite3_finalize(stmt);

    // Send a success response
    request->send(200, "text/plain", "Task saved successfully");
});
// Handle fetching tasks via GET request
server.on("/tasks", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Check if the request has the "sessionId" header
    if (!request->hasHeader("sessionId")) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }

    // Extract the session ID from the request headers
    String sessionId = request->getHeader("sessionId")->value();

    // Validate session ID
    if (sessions.find(sessionId) == sessions.end()) {
        request->send(401, "text/plain", "Invalid session ID");
        return;
    }

    // Get the username associated with the session ID
    String username = sessions[sessionId].username;

    // Prepare the SQL statement to select tasks associated with the user from the tasks table
    String sql = "SELECT task_id, tasks FROM tasks WHERE user_id = (SELECT user_id FROM users WHERE username = ?);";
    sqlite3_stmt *stmt;
    int result = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL);
    if (result != SQLITE_OK) {
        Serial.println("Failed to prepare SQL statement");
        request->send(500, "text/plain", "Server Error");
        return;
    }

    // Bind parameters to the SQL statement
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    // Execute the SQL statement
    AsyncResponseStream *response = request->beginResponseStream("text/html");
    response->print("<ul>");

    // Now, integrate the code snippet into your response generation loop
    while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char *taskId = (const char *)sqlite3_column_text(stmt, 0);
    const char *task = (const char *)sqlite3_column_text(stmt, 1);
    response->printf("<li id='%s'>%s <button class='edit-btn' data-task-id='%s'>Edit</button> <button class='delete-btn' data-task-id='%s'>Delete</button></li>", taskId, task, taskId, taskId);
}

    response->print("</ul>");

    // Finalize the SQL statement
    sqlite3_finalize(stmt);

    // Send the response
    request->send(response);
});

// Handle deleting tasks via POST request
server.on("/delete", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Check if the request has the "sessionId" header
    if (!request->hasHeader("sessionId")) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }

    // Extract the session ID from the request headers
    String sessionId = request->getHeader("sessionId")->value();

    // Validate session ID
    if (sessions.find(sessionId) == sessions.end()) {
        request->send(401, "text/plain", "Invalid session ID");
        return;
    }

    // Get the task ID from the request parameters
    if (!request->hasParam("taskId", true)) {
        request->send(400, "text/plain", "Bad Request: Missing 'taskId' parameter");
        return;
    }

    String taskId = request->getParam("taskId", true)->value();

    // Prepare the SQL statement to delete the task from the tasks table
    String sql = "DELETE FROM tasks WHERE task_id = ?";
    sqlite3_stmt *stmt;
    int result = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL);
    if (result != SQLITE_OK) {
        Serial.println("Failed to prepare SQL statement");
        request->send(500, "text/plain", "Server Error");
        return;
    }

    // Bind parameters to the SQL statement
    sqlite3_bind_text(stmt, 1, taskId.c_str(), -1, SQLITE_TRANSIENT);

    // Execute the SQL statement
    result = sqlite3_step(stmt);
    if (result != SQLITE_DONE) {
        Serial.println("Failed to delete task from database");
        request->send(500, "text/plain", "Server Error: Failed to delete task from database");
        return;
    }

    // Finalize the SQL statement
    sqlite3_finalize(stmt);

    // Send a success response
    request->send(200, "text/plain", "Task deleted successfully");
});

// Handle editing tasks via POST request
server.on("/edit", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Check if the request has the "sessionId" header
    if (!request->hasHeader("sessionId")) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }

    // Extract the session ID from the request headers
    String sessionId = request->getHeader("sessionId")->value();

    // Validate session ID
    if (sessions.find(sessionId) == sessions.end()) {
        request->send(401, "text/plain", "Invalid session ID");
        return;
    }

    // Get the task ID and updated task text from the request parameters
    if (!request->hasParam("taskId", true) || !request->hasParam("taskText", true)) {
        request->send(400, "text/plain", "Bad Request: Missing 'taskId' or 'taskText' parameter");
        return;
    }

    String taskId = request->getParam("taskId", true)->value();
    String taskText = request->getParam("taskText", true)->value();

    // Prepare the SQL statement to update the task in the tasks table
    String sql = "UPDATE tasks SET tasks = ? WHERE task_id = ?";
    sqlite3_stmt *stmt;
    int result = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL);
    if (result != SQLITE_OK) {
        Serial.println("Failed to prepare SQL statement");
        request->send(500, "text/plain", "Server Error");
        return;
    }

    // Bind parameters to the SQL statement
    sqlite3_bind_text(stmt, 1, taskText.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, taskId.c_str(), -1, SQLITE_TRANSIENT);

    // Execute the SQL statement
    result = sqlite3_step(stmt);
    if (result != SQLITE_DONE) {
        Serial.println("Failed to update task in database");
        request->send(500, "text/plain", "Server Error: Failed to update task in database");
        return;
    }

    // Finalize the SQL statement
    sqlite3_finalize(stmt);

    // Send a success response
    request->send(200, "text/plain", "Task updated successfully");
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
