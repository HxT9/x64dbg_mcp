using System.Text;
using System.Text.Json;

namespace X64DbgMcp;

/// <summary>
/// Thin HTTP client for the x64dbg_mcp plugin's local JSON-RPC endpoint.
/// The plugin listens on 127.0.0.1:&lt;port&gt; (default 8745); override with the
/// X64DBG_MCP_URL environment variable.
/// </summary>
public sealed class X64DbgClient
{
    private readonly HttpClient _http;
    private readonly string _baseUrl;

    public X64DbgClient()
    {
        _baseUrl = Environment.GetEnvironmentVariable("X64DBG_MCP_URL") ?? "http://127.0.0.1:8745/";
        if (!_baseUrl.EndsWith('/'))
            _baseUrl += "/";
        _http = new HttpClient { Timeout = TimeSpan.FromSeconds(30) };
    }

    /// <summary>Invoke a plugin RPC method and return its "result" element.</summary>
    public async Task<JsonElement> CallAsync(string method, object? @params = null, CancellationToken ct = default)
    {
        var payload = new { method, @params = @params ?? new { } };
        // Serialize to a string and use StringContent so the request carries a
        // Content-Length header. (PostAsJsonAsync streams the body with
        // Transfer-Encoding: chunked, which the plugin's minimal HTTP server
        // does not parse — it would receive an empty body.)
        var json = JsonSerializer.Serialize(payload);
        using var content = new StringContent(json, Encoding.UTF8, "application/json");
        HttpResponseMessage resp;
        try
        {
            resp = await _http.PostAsync(_baseUrl, content, ct);
        }
        catch (Exception ex)
        {
            throw new InvalidOperationException(
                $"Cannot reach the x64dbg plugin at {_baseUrl}. Is x64dbg running with the x64dbg_mcp plugin loaded? ({ex.Message})");
        }

        var text = await resp.Content.ReadAsStringAsync(ct);
        using var doc = JsonDocument.Parse(text);
        var root = doc.RootElement;

        if (root.TryGetProperty("ok", out var ok) && ok.GetBoolean())
            return root.GetProperty("result").Clone();

        var err = root.TryGetProperty("error", out var e) ? e.GetString() : "unknown error";
        throw new InvalidOperationException($"x64dbg error: {err}");
    }

    /// <summary>Convenience: call a method and return the result serialized as compact JSON text.</summary>
    public async Task<string> CallJsonAsync(string method, object? @params = null, CancellationToken ct = default)
    {
        var result = await CallAsync(method, @params, ct);
        return result.GetRawText();
    }
}
