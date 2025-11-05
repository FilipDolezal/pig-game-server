package org.example;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.Socket;

public class Client {

    private final String host;
    private final int port;
    private Socket socket;
    private OutputStream out;
    private InputStream in;

    public Client(String host, int port) {
        this.host = host;
        this.port = port;
    }

    public void connect() throws IOException {
        socket = new Socket(host, port);
        out = socket.getOutputStream();
        in = socket.getInputStream();
    }

    public void disconnect() throws IOException {
        if (socket != null) {
            socket.close();
        }
    }

    public void sendMessage(MessageType type, String payload) throws IOException {
        byte[] messageBytes = new byte[260]; // 4 bytes for type, 256 for payload
        messageBytes[0] = (byte) type.ordinal();
        byte[] payloadBytes = payload.getBytes();
        System.arraycopy(payloadBytes, 0, messageBytes, 4, payloadBytes.length);
        out.write(messageBytes);
    }

    public Message receiveMessage() throws IOException {
        byte[] typeBytes = new byte[4];
        if (in.read(typeBytes) == -1) {
            return null;
        }
        int typeOrdinal = typeBytes[0];
        MessageType type = MessageType.values()[typeOrdinal];

        byte[] payloadBytes = new byte[256];
        in.read(payloadBytes);
        String payload = new String(payloadBytes).trim();

        return new Message(type, payload);
    }

    public static void main(String[] args) {
        Client client = new Client("localhost", 12345);
        try {
            client.connect();
            System.out.println("Connected to server.");

            // Say hello
            client.sendMessage(MessageType.MSG_HELLO, "");
            Message response = client.receiveMessage();
            System.out.println("Server says: " + response.getPayload());

            // Echo a message
            client.sendMessage(MessageType.MSG_ECHO, "Hello from client!");
            response = client.receiveMessage();
            System.out.println("Server echoes: " + response.getPayload());

            // Quit
            client.sendMessage(MessageType.MSG_QUIT, "");
            System.out.println("Disconnected from server.");

            client.disconnect();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }
}

enum MessageType {
    MSG_HELLO,
    MSG_ECHO,
    MSG_QUIT
}

class Message {
    private final MessageType type;
    private final String payload;

    public Message(MessageType type, String payload) {
        this.type = type;
        this.payload = payload;
    }

    public MessageType getType() {
        return type;
    }

    public String getPayload() {
        return payload;
    }
}
