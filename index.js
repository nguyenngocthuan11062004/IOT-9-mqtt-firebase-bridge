// ================= MQTT (HiveMQ Cloud) =================
const mqtt = require("mqtt");

const options = {
  host: "96ad0e1c02e145399b29a23b82373b6f.s1.eu.hivemq.cloud",
  port: 8883,
  protocol: "mqtts",
  username: "IOT_9",
  password: "Thuannguyen123@"
};

const client = mqtt.connect(options);

const admin = require("firebase-admin");
const serviceAccount = require("./serviceAccountKey.json");

admin.initializeApp({
  credential: admin.credential.cert(serviceAccount),
  databaseURL: "https://iot-9-931f3-default-rtdb.firebaseio.com"
});

const db = admin.database();

// 1. Sub MQTT Topics từ ESP32

client.on("connect", () => {
  console.log("✅ Connected to HiveMQ");

  client.subscribe("esp32/temperature");
  client.subscribe("esp32/humidity");
  client.subscribe("esp32/rain_status");
  client.subscribe("esp32/relay_state");

  console.log("📡 Subscribed to ESP32 topics");
});

// 2. ESP32 → MQTT → Firebase (Sensor data)

client.on("message", async (topic, messageBuffer) => {
  const message = messageBuffer.toString();
  console.log(`📥 MQTT [${topic}]: ${message}`);

  let path = null;

  if (topic === "esp32/temperature") path = "sensor/temperature";
  if (topic === "esp32/humidity")    path = "sensor/humidity";
  if (topic === "esp32/rain_status") path = "sensor/rain_status";
  if (topic === "esp32/relay_state") path = "sensor/relay";

  if (!path) return;

  await db.ref(path).set({
    value: message,
    timestamp: Date.now()
  });

  console.log(`🔥 Firebase updated: ${path}`);
});

// 3. Firebase → MQTT (Relay control)

db.ref("sensor/relay").on("value", (snapshot) => {
  let relayState = snapshot.val();
  if (!relayState) return;

  if (typeof relayState === "object" && relayState.value) {
    relayState = relayState.value.toUpperCase();
  } else {
    console.log("⚠️ Invalid relayState:", relayState);
    return;
  }

  console.log("🔁 Firebase relay changed →", relayState);

  if (relayState === "ON") {
    client.publish("esp32/relay", "ON");
    console.log("➡️ Sent MQTT: relay ON");
  }

  if (relayState === "OFF") {
    client.publish("esp32/relay", "OFF");
    console.log("➡️ Sent MQTT: relay OFF");
  }
});

// ========================================================
// 4. SCHEDULE ENGINE – CHECK LỊCH MỖI PHÚT
// ========================================================

console.log("⏰ Schedule engine started");

setInterval(async () => {
  try {
    const snapshot = await db.ref("schedules").once("value");
    const schedules = snapshot.val();
    if (!schedules) return;

    const now = new Date();
    const hour = now.getHours();
    const minute = now.getMinutes();
    const todayKey = `${now.getFullYear()}-${now.getMonth() + 1}-${now.getDate()}`;

    for (const [id, schedule] of Object.entries(schedules)) {
      if (!schedule.enabled) continue;

      // Chống chạy lại nhiều lần trong cùng ngày
      if (schedule.last_run === todayKey) continue;

      if (
        schedule.hour === hour &&
        schedule.minute === minute
      ) {
        console.log(`⏳ Trigger schedule: ${id}`);

        // BẬT BƠM
        await db.ref("sensor/relay").set({
          value: "ON",
          timestamp: Date.now()
        });

        // Lưu last_run
        await db.ref(`schedules/${id}/last_run`).set(todayKey);

        // TẮT SAU duration_sec
        setTimeout(async () => {
          await db.ref("sensor/relay").set({
            value: "OFF",
            timestamp: Date.now()
          });
          console.log(`✅ Schedule ${id} finished`);
        }, (schedule.duration_sec || 300) * 1000);
      }
    }
  } catch (err) {
    console.error("❌ Schedule engine error:", err.message);
  }
}, 60 * 1000);
