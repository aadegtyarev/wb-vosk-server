import os
import asyncio
import json
import websockets
import paho.mqtt.client as mqtt
from vosk import Model, KaldiRecognizer
import uuid
import time
import logging
import concurrent.futures
from dotenv import load_dotenv
from typing import Any, Dict, List

# Load configuration from .env file
load_dotenv()

# Configuration from environment variables
LOG_LEVEL = os.environ.get("LOG_LEVEL", "INFO").upper()
logging.basicConfig(level=LOG_LEVEL, format='[%(levelname)s] %(message)s')
logger = logging.getLogger(__name__)

MQTT_HOST = os.environ.get("MQTT_HOST", "192.168.2.116")
MQTT_PORT = int(os.environ.get("MQTT_PORT", "1883"))
MQTT_LOGIN = os.environ.get("MQTT_LOGIN", "")
MQTT_PASSWORD = os.environ.get("MQTT_PASSWORD", "")
DEVICE_NAME = os.environ.get("DEVICE_NAME", "wb-vosk-device")

WEBSOCKET_HOST = os.environ.get("WEBSOCKET_HOST", "0.0.0.0")
WEBSOCKET_PORT = int(os.environ.get("WEBSOCKET_PORT", "8765"))

MODEL_PATH = os.environ.get("MODEL_PATH", "/opt/vosk-model-ru/model")
INACTIVITY_TIMEOUT = float(os.environ.get("INACTIVITY_TIMEOUT", "0.5"))
BUFFER_SIZE = int(os.environ.get("BUFFER_SIZE", "8000"))
MIN_VALID_LENGTH = int(os.environ.get("MIN_VALID_LENGTH", "3"))
MAX_WORKERS = int(os.environ.get("MAX_WORKERS", "4"))
SILENCE_CHUNK = b"\x00" * BUFFER_SIZE

# Setup MQTT client
mqtt_client = mqtt.Client()
if MQTT_LOGIN and MQTT_PASSWORD:
    mqtt_client.username_pw_set(MQTT_LOGIN, MQTT_PASSWORD)
mqtt_client.connect(MQTT_HOST, MQTT_PORT)
mqtt_client.loop_start()

class MqttHandler:
    """
    Handles MQTT connection, publishing, subscription, and device cleanup.
    """
    def __init__(self, device_name: str, logger: logging.Logger) -> None:
        self.device_name = device_name
        self.logger = logger
        self.recognition_enabled = True
        self.topics: List[str] = []
        self.client = mqtt_client
        self.client.on_message = self.on_message
        # Subscribe to control topic for recognition
        self.client.subscribe(f"/devices/{self.device_name}/controls/Recognition/on")
        self.setup_device()

    def publish(self, topic: str, payload: Any) -> None:
        """
        Publishes an MQTT message and records the topic.
        """
        full_topic = f"/devices/{self.device_name}/controls/{topic}"
        self.client.publish(full_topic, json.dumps(payload, ensure_ascii=False), retain=True)
        if full_topic not in self.topics:
            self.topics.append(full_topic)

    def setup_device(self) -> None:
        """
        Creates a virtual MQTT device by publishing meta and control data.
        """
        device_meta = {
            "driver": "wb-vosk-device",
            "title": {"en": "Speech Recognition", "ru": "Распознавание речи"}
        }
        self.publish("meta", device_meta)
        controls_meta = {
            "Recognition": {
                "type": "switch",
                "order": 1,
                "title": {"en": "Recognition", "ru": "Распознавание"}
            },
            "Text": {
                "type": "text",
                "order": 2,
                "readonly": True,
                "title": {"en": "Recognized Text", "ru": "Распознанный текст"}
            }
        }
        for control, meta in controls_meta.items():
            self.publish(f"{control}/meta", meta)
        # Default: recognition enabled
        self.publish("Recognition", 1)
        self.publish("Text", "")

    def on_message(self, client, userdata, msg) -> None:
        """
        Updates the recognition flag based on messages received on the /on topic.
        """
        if msg.topic == f"/devices/{self.device_name}/controls/Recognition/on":
            value = msg.payload.decode()
            self.recognition_enabled = (value == "1")
            state = "On" if self.recognition_enabled else "Off"
            self.logger.info(f"MQTT Recognition set to: {state}")

    def clear_topics(self) -> None:
        """
        Clears all published MQTT topics (by sending None with retain=True).
        """
        for topic in self.topics:
            self.client.publish(topic, None, retain=True)
        self.logger.info("MQTT: All topics cleared.")

    def shutdown(self) -> None:
        """
        Shuts down the MQTT connection.
        """
        self.clear_topics()
        self.client.disconnect()
        self.client.loop_stop()

mqtt_handler = MqttHandler(DEVICE_NAME, logger)

# Load Vosk model once and create an executor for blocking operations.
model = Model(MODEL_PATH)
executor = concurrent.futures.ThreadPoolExecutor(max_workers=MAX_WORKERS)

async def flush_buffer(recognizer: KaldiRecognizer, client_id: str) -> None:
    """
    Flushes the accumulated buffer by injecting silence and processing the final result.
    """
    loop = asyncio.get_running_loop()
    await loop.run_in_executor(executor, recognizer.AcceptWaveform, SILENCE_CHUNK)
    result = await loop.run_in_executor(executor, recognizer.FinalResult)
    text = json.loads(result).get("text", "").strip()
    if len(text) >= MIN_VALID_LENGTH:
        payload = {"client_id": client_id, "timestamp": int(time.time()), "text": text}
        mqtt_handler.publish("Text", payload)
        logger.debug(f"Flush result for {client_id}: {text}")

async def process_client(websocket: websockets.WebSocketServerProtocol) -> None:
    """
    Handles a single client's connection:
      - Retrieves or generates a client_id.
      - Creates a recognizer instance.
      - Accumulates incoming byte data in a local buffer.
      - Flushes the buffer on reaching BUFFER_SIZE or inactivity timeout.
      - If recognition is disabled via MQTT, data is not passed to vosk.
    """
    logger.info(f"New client connected: {websocket.remote_address}")
    try:
        first_message = await websocket.recv()
        data = json.loads(first_message)
        client_id = data.get("client_id", str(uuid.uuid4()))
    except Exception:
        client_id = str(uuid.uuid4())
    logger.info(f"Client {client_id} connected")
    
    recognizer = KaldiRecognizer(model, 16000)
    buffer = b""
    loop = asyncio.get_running_loop()

    while True:
        try:
            data = await asyncio.wait_for(websocket.recv(), timeout=INACTIVITY_TIMEOUT)
        except asyncio.TimeoutError:
            if buffer:
                if mqtt_handler.recognition_enabled:
                    await flush_buffer(recognizer, client_id)
                else:
                    logger.debug(f"Recognition disabled for {client_id}: skipping flush on timeout.")
                buffer = b""
            continue
        except websockets.ConnectionClosed:
            break

        if not data or not isinstance(data, bytes):
            continue
        buffer += data
        if len(buffer) >= BUFFER_SIZE:
            if mqtt_handler.recognition_enabled:
                accepted = await loop.run_in_executor(executor, recognizer.AcceptWaveform, buffer)
                if accepted:
                    await flush_buffer(recognizer, client_id)
            else:
                logger.debug(f"Recognition disabled for {client_id}: skipping processing on buffer full.")
            buffer = b""

    if buffer:
        if mqtt_handler.recognition_enabled:
            await flush_buffer(recognizer, client_id)
        else:
            logger.debug(f"Recognition disabled for {client_id}: skipping final flush.")
    logger.info(f"Client {client_id} disconnected.")

async def main() -> None:
    async with websockets.serve(process_client, WEBSOCKET_HOST, WEBSOCKET_PORT):
        logger.info(f"WebSocket server running at ws://{WEBSOCKET_HOST}:{WEBSOCKET_PORT}")
        await asyncio.Future()  # run forever

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        logger.info("Shutting down...")
    finally:
        mqtt_handler.shutdown()
        executor.shutdown(wait=True)
