from flask import Flask, request, jsonify

app = Flask(__name__)

# "Base de datos" simple (puede ser JSON o diccionario)
users = {
    "sara": {"password": "1234", "role": "operator"},
    "admin": {"password": "admin", "role": "admin"}
}

@app.route('/auth', methods=['POST'])
def auth():
    data = request.get_json()

    username = data.get("username")
    password = data.get("password")

    if username in users and users[username]["password"] == password:
        return jsonify({
            "status": "ok",
            "role": users[username]["role"]
        }), 200
    else:
        return jsonify({
            "status": "error",
            "message": "Invalid credentials"
        }), 401

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)