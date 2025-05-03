from io import BytesIO
from flask import Flask, jsonify, request, send_file
from flask_cors import CORS
from pydub import AudioSegment
from google import genai
from google.genai import types
from base64 import b64decode
app = Flask(__name__)
CORS(app)

import torch
from parler_tts import ParlerTTSForConditionalGeneration
from transformers import AutoTokenizer
import soundfile as sf


class Board:
  def __init__(self, board_id, name, ip):
    self.board_id = board_id
    self.name = name
    self.ip = ip
    self.queue = []

boards = {}

@app.route('/update', methods=['POST'])
def handle_file():
    board_id = request.args.get('board_id', 'esp32_1')
    name = request.args.get('name', 'ESP32')
    file = request.get_data()  # 直接取得 raw binary
    print(board_id)
    print(name)
    print(request.remote_addr)
    print(len(file))
    if board_id not in boards:
        boards[board_id] = Board(board_id, name, request.remote_addr)
    else:
        boards[board_id].name = name
        boards[board_id].ip = request.remote_addr
    for board in boards.values():
        board.queue.append(file)
    print(len(board.queue))
    return jsonify({"message": "Raw file received successfully"}), 200
      
@app.route('/get_voices', methods=['GET'])
def get_voices():
  board_id = request.args.get('board_id')
  if board_id not in boards:
    print("Board not found")
    return jsonify({"error": "Board not found"}), 404
  board = boards[board_id]
  if not board.queue:
    print("Empty queue")
    return jsonify({"error": "Queue is empty"}), 404
  print(len(board.queue))
  merged_audio = AudioSegment.empty()
  for raw_wav_data in board.queue[:2] if (len(board.queue) > 2) else board.queue:
    try:
      audio = AudioSegment.from_file(BytesIO(raw_wav_data), format="wav")
      merged_audio += audio
    except Exception as e:
      return jsonify({"error": f"Failed to process audio in queue: {str(e)}"}), 500
  board.queue = board.queue[2:] if len(board.queue) > 2 else []
  merged_audio.export("./thiswav.wav", format="wav")
  buffer = BytesIO()
  merged_audio.export(buffer, format="wav")
  buffer.seek(0)
  return send_file(
        buffer,
        mimetype="audio/wav",
        as_attachment=True,
        download_name="merged_output.wav"
    )

# API to distribute boards

gemini_api_key = "AIzaSyBm_iR_y9lPRkwyCKpy_LbFqP4rvjLFAdg"

gemini_client = genai.Client(api_key=gemini_api_key)

# TTS_key = "c6016c5c-3970-46aa-b9f0-8284ba7ad3e8"
# TTS_url = "https://client.camb.ai/apis/tts"
# headers = {
#     "Accept": "application/json",
#     "x-api-key": API_KEY,
#     "Content-Type": "application/json"
# }

@app.route('/ChatWithGPT', methods=['POST'])
def fetch_api():
  file = request.get_data()  # 直接取得 raw binary
  if not file or len(file) == 0:
      return jsonify({"error": "No file provided"}), 400

  # file = request.data['file']
  stt_response = gemini_client.models.generate_content(
    model="gemini-2.0-flash",
    contents=[
      "你是一個語音助手，與駕駛者進行語音對話。使用自然、簡潔的語氣回應問題或聊天話題。你不需要回應過於長的說明，並避免過多細節。駕駛者可能會問你問題、閒聊或抱怨交通狀況。保持語氣友善、輕鬆，並根據駕駛者語音內容回覆。",
      types.Part.from_bytes(
        data = file,
        mime_type="audio/wav",
      )
    ]
  )
  print(stt_response.text)
  return jsonify({"text": stt_response.text}), 200
  # device = "cuda:0" if torch.cuda.is_available() else "cpu"
  # model = ParlerTTSForConditionalGeneration.from_pretrained("parler-tts/parler-tts-mini-v1").to(device)
  # tokenizer = AutoTokenizer.from_pretrained("parler-tts/parler-tts-mini-v1")
    

if __name__ == '__main__':
  app.run(debug=True,port=1145,host='192.168.18.96')