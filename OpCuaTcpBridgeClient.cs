using System;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using UnityEngine;

/// <summary>
/// Client for connecting to the OPC UA TCP Bridge and receiving sensor data frames.
/// </summary>
public class OpCuaTcpBridgeClient : MonoBehaviour
{
    [Header("Connection Settings")]
    [SerializeField] private string serverHost = "localhost";
    [SerializeField] private int serverPort = 9000;
    [SerializeField] private bool autoConnectOnStart = true;

    [Header("Events")]
    public SensorDataEvent OnSensorDataReceived;

    private TcpClient tcpClient;
    private NetworkStream stream;
    private CancellationTokenSource cancellationTokenSource;
    private bool isConnected = false;
    private Task receiveTask;

    /// <summary>
    /// Represents a decoded sensor data frame.
    /// </summary>
    [Serializable]
    public class SensorData
    {
        public byte version;
        public ulong sequence;
        public ulong timestampMs;
        public bool sensor1;
        public bool sensor2;
        public bool sensor3;
        public bool sensor4;

        /// <summary>
        /// Converts timestamp from milliseconds to DateTime.
        /// </summary>
        public DateTime GetTimestamp()
        {
            return DateTimeOffset.FromUnixTimeMilliseconds((long)timestampMs).DateTime;
        }

        public override string ToString()
        {
            return $"Seq: {sequence}, Time: {GetTimestamp():HH:mm:ss.fff}, " +
                   $"Sensors: [{sensor1}, {sensor2}, {sensor3}, {sensor4}]";
        }
    }

    /// <summary>
    /// Event delegate for sensor data updates.
    /// </summary>
    [Serializable]
    public class SensorDataEvent : UnityEngine.Events.UnityEvent<SensorData> { }

    private void Start()
    {
        if (autoConnectOnStart)
        {
            Connect();
        }
    }

    private void OnDestroy()
    {
        Disconnect();
    }

    private void OnApplicationQuit()
    {
        Disconnect();
    }

    /// <summary>
    /// Connects to the TCP bridge server.
    /// </summary>
    public async void Connect()
    {
        if (isConnected)
        {
            Debug.LogWarning("Already connected to TCP bridge");
            return;
        }

        try
        {
            cancellationTokenSource = new CancellationTokenSource();
            tcpClient = new TcpClient();
            
            Debug.Log($"Connecting to TCP bridge at {serverHost}:{serverPort}...");
            await tcpClient.ConnectAsync(serverHost, serverPort);
            
            stream = tcpClient.GetStream();
            isConnected = true;
            
            Debug.Log("Connected to TCP bridge successfully");
            
            receiveTask = ReceiveFramesAsync(cancellationTokenSource.Token);
        }
        catch (Exception ex)
        {
            Debug.LogError($"Failed to connect to TCP bridge: {ex.Message}");
            Disconnect();
        }
    }

    /// <summary>
    /// Disconnects from the TCP bridge server.
    /// </summary>
    public void Disconnect()
    {
        isConnected = false;
        
        cancellationTokenSource?.Cancel();
        
        try
        {
            stream?.Close();
            tcpClient?.Close();
        }
        catch (Exception ex)
        {
            Debug.LogWarning($"Error during disconnect: {ex.Message}");
        }
        finally
        {
            stream = null;
            tcpClient = null;
            cancellationTokenSource?.Dispose();
            cancellationTokenSource = null;
        }
        
        Debug.Log("Disconnected from TCP bridge");
    }

    /// <summary>
    /// Continuously receives and decodes frames from the TCP stream.
    /// </summary>
    private async Task ReceiveFramesAsync(CancellationToken cancellationToken)
    {
        const int FRAME_SIZE = 18;
        byte[] buffer = new byte[FRAME_SIZE];
        int bytesRead = 0;
        int offset = 0;

        try
        {
            while (isConnected && !cancellationToken.IsCancellationRequested)
            {
                int remaining = FRAME_SIZE - offset;
                int read = await stream.ReadAsync(buffer, offset, remaining, cancellationToken);
                
                if (read == 0)
                {
                    Debug.LogWarning("TCP stream closed by server");
                    break;
                }

                bytesRead += read;
                offset += read;

                if (bytesRead == FRAME_SIZE)
                {
                    SensorData data = DecodeFrame(buffer);
                    if (data != null)
                    {
                        OnSensorDataReceived?.Invoke(data);
                    }

                    bytesRead = 0;
                    offset = 0;
                }
            }
        }
        catch (OperationCanceledException)
        {
            Debug.Log("Receive task cancelled");
        }
        catch (Exception ex)
        {
            Debug.LogError($"Error receiving frames: {ex.Message}");
        }
        finally
        {
            isConnected = false;
        }
    }

    /// <summary>
    /// Decodes a 18-byte frame into a SensorData object.
    /// </summary>
    private SensorData DecodeFrame(byte[] frame)
    {
        if (frame == null || frame.Length < 18)
        {
            Debug.LogError("Invalid frame: too short");
            return null;
        }

        try
        {
            SensorData data = new SensorData
            {
                version = frame[0],
                sequence = ReadUInt64BigEndian(frame, 1),
                timestampMs = ReadUInt64BigEndian(frame, 9),
            };

            byte flags = frame[17];
            data.sensor1 = (flags & 0x01) != 0;
            data.sensor2 = (flags & 0x02) != 0;
            data.sensor3 = (flags & 0x04) != 0;
            data.sensor4 = (flags & 0x08) != 0;

            return data;
        }
        catch (Exception ex)
        {
            Debug.LogError($"Error decoding frame: {ex.Message}");
            return null;
        }
    }

    /// <summary>
    /// Reads a big-endian uint64 from the buffer starting at the specified offset.
    /// </summary>
    private ulong ReadUInt64BigEndian(byte[] buffer, int offset)
    {
        if (offset + 8 > buffer.Length)
        {
            throw new ArgumentException("Buffer too short for uint64");
        }

        ulong value = 0;
        for (int i = 0; i < 8; i++)
        {
            value = (value << 8) | buffer[offset + i];
        }

        return value;
    }

    /// <summary>
    /// Gets the current connection status.
    /// </summary>
    public bool IsConnected => isConnected;
}
