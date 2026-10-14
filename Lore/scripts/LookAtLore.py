import os
import http.server
import socketserver

class NoCacheDirectoryHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
        self.send_header('Pragma', 'no-cache')
        self.send_header('Expires', '0')
        super().end_headers()

    def translate_path(self, path):
        # Override to prevent appending 'index.html' for directories
        path = super().translate_path(path)
        if os.path.isdir(path):
            # Instead of appending index.html, we'll serve the directory listing
            return path
        return path

    def do_GET(self):
        # Force directory listing if the path is a directory
        f = self.send_head()
        if f:
            try:
                self.copyfile(f, self.wfile)
            finally:
                f.close()
        else:
            # If it's a directory and no file was sent, list it
            if os.path.isdir(self.path.rstrip('/') or '.'):
                self.list_directory(self.path)

    def list_directory(self, path):
        # Ensure we always list directories
        return super().list_directory(path)

# Get the directory of the script (with fallback for interactive mode)
try:
    script_dir = os.path.dirname(os.path.abspath(__file__))
except NameError:
    script_dir = os.getcwd()

# Move one directory up (to Lore/ if script is in Lore/scripts/)
parent_dir = os.path.abspath(os.path.join(script_dir, '..'))

# Change the current working directory to the parent directory
os.chdir(parent_dir)

# Print debugging info
print(f"Current working directory: {os.getcwd()}")
print("Files and directories in this folder:")
for item in os.listdir('.'):
    print(f"- {item}")

# Set up the server
PORT = 8000
Handler = NoCacheDirectoryHTTPRequestHandler

with socketserver.TCPServer(("", PORT), Handler) as httpd:
    print(f"\nServing HTTP on port {PORT} from directory: {parent_dir}")
    print("Navigate to http://localhost:8000/ in your browser to see the directory listing.")
    httpd.serve_forever()