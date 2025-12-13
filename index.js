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

// ================= Firebase =================
const admin = require("firebase-admin");
const serviceAccount = require("./serviceAccountKey.json");

admin.initializeApp({
  credential: admin.credential.cert(serviceAccount),
  databaseURL: "https://iot-9-931f3-default-rtdb.firebaseio.com"
});

const db = admin.database();

// ========================================================
// 1. Sub MQTT Topics từ ESP32
// ========================================================

client.on("connect", () => {
  console.log("Connected to HiveMQ");

  client.subscribe("esp32/temperature");
  client.subscribe("esp32/humidity");
  client.subscribe("esp32/rain_status");
  client.subscribe("esp32/relay_state");

  console.log("Subscribed to ESP32 topics");
});

// ========================================================
// 2. Khi nhận data từ ESP32 → đưa vào Firebase
// ========================================================

client.on("message", async (topic, messageBuffer) => {
  const message = messageBuffer.toString();
  console.log(`MQTT [${topic}]: ${message}`);

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

  console.log(`Firebase updated: ${path}`);
});

// ========================================================
// 3. LẮNG NGHE Firebase → Điều khiển ESP32
// ========================================================
db.ref("sensor/relay").on("value", (snapshot) => {
  let relayState = snapshot.val();
  if (!relayState) return;

  // Kiểm tra nếu relayState là một đối tượng, lấy giá trị "value"
  if (typeof relayState === "object" && relayState.value) {
    relayState = relayState.value.toUpperCase();  // Lấy giá trị và chuyển thành chữ hoa
  } else {
    console.log("relayState is not an object with a value:", relayState);
    return;  // Nếu không phải đối tượng với trường "value", bỏ qua
  }

  console.log("Firebase relay state changed →", relayState);

  // Gửi lệnh xuống ESP32 qua MQTT khi trạng thái thay đổi
  if (relayState === "ON") {
    client.publish("esp32/relay", "ON");
    console.log("Sent MQTT: relay ON");
  }

  if (relayState === "OFF") {
    client.publish("esp32/relay", "OFF");
    console.log("Sent MQTT: relay OFF");
  }
});