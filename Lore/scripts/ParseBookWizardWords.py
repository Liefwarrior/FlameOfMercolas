from flask import Flask, render_template_string, request
import re
from pathlib import Path

app = Flask(__name__)

# File path using pathlib (one level above)
file_path = Path('..') / 'Lord%20of%20Trojia%20(indexable).txt'

# If file not present, instruct user
if not file_path.exists():
    print("The book file '../Lord of Trojia (indexable).txt' is not found.")
    print("Since the repository is private, please manually download 'Lord of Trojia (indexable).txt' from your GitHub repo")
    print("and place it one level above this script (in the parent directory).")
    print("Then run the script again.")
    exit(1)

# Read the book text
with open(file_path, 'r', encoding='utf-8') as f:
    text = f.read()

# Extract original words: Capitalized words (proper nouns), excluding common ones
words = re.findall(r'\b[A-Z][a-zA-Z]*\b', text)
unique_words = set(words)

common_words = {
    'The', 'A', 'An', 'Of', 'In', 'To', 'And', 'But', 'Or', 'For', 'On', 'With', 
    'As', 'At', 'By', 'From', 'He', 'She', 'It', 'They', 'We', 'You', 'I', 
    'This', 'That', 'His', 'Her', 'Their', 'My', 'Your', 'Our', 'Chapter', 'Part'
}

original_words = [w for w in unique_words if w not in common_words and len(w) > 2]

# Dictionary: word -> list of up to 5 sentences containing the word
dictionary = {}
for word in original_words:
    # Find sentences containing the word
    sentences = re.findall(r'[^.!?]*\b' + re.escape(word) + r'\b[^.!?]*[.!?]', text)
    dictionary[word] = sentences[:5]  # Limit to 5 for brevity

@app.route('/', methods=['GET', 'POST'])
def index():
    search_query = request.form.get('search', '').lower()
    filtered_words = [w for w in sorted(dictionary.keys()) if search_query in w.lower()]
    return render_template_string("""
    <!DOCTYPE html>
    <html lang="en">
    <head>
        <meta charset="UTF-8">
        <title>Original Words Dictionary</title>
        <style>
            body { font-family: Arial, sans-serif; margin: 40px; background-color: #f4f4f4; }
            h1 { color: #333; }
            form { margin-bottom: 20px; }
            input[type="text"] { padding: 8px; width: 300px; }
            button { padding: 8px 12px; background: #007BFF; color: white; border: none; cursor: pointer; }
            ul { list-style-type: none; padding: 0; }
            li { margin: 10px 0; background: white; padding: 10px; border-radius: 5px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
            a { text-decoration: none; color: #007BFF; font-weight: bold; }
            a:hover { text-decoration: underline; }
        </style>
    </head>
    <body>
        <h1>Original Words from the Book</h1>
        <form method="POST">
            <input type="text" name="search" placeholder="Search words..." value="{{ search_query }}">
            <button type="submit">Search</button>
        </form>
        <ul>
        {% for word in filtered_words %}
            <li><a href="/word/{{ word }}">{{ word }}</a></li>
        {% endfor %}
        </ul>
    </body>
    </html>
    """, filtered_words=filtered_words, search_query=search_query)

@app.route('/word/<word>')
def show_word(word):
    contexts = dictionary.get(word, [])
    html = """
    <!DOCTYPE html>
    <html lang="en">
    <head>
        <meta charset="UTF-8">
        <title>{{ word }} Details</title>
        <style>
            body { font-family: Arial, sans-serif; margin: 40px; background-color: #f4f4f4; }
            h1 { color: #333; }
            ul { list-style-type: disc; margin-left: 20px; }
            li { margin: 10px 0; font-style: italic; color: #555; }
            a { text-decoration: none; color: #007BFF; }
            a:hover { text-decoration: underline; }
        </style>
    </head>
    <body>
        <h1>{{ word }}</h1>
        <h2>Usage Contexts (Material Properties, Descriptions, etc.)</h2>
        <ul>
    """
    for sent in contexts:
        html += f"<li>{sent.strip()}</li>"
    html += """
        </ul>
        <a href="/">Back to List</a>
    </body>
    </html>
    """
    return render_template_string(html, word=word)

if __name__ == '__main__':
    app.run(debug=True, port=5000)