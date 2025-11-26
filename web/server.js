const express = require('express');
const sqlite3 = require('sqlite3').verbose();
const bcrypt = require('bcrypt');
const jwt = require('jsonwebtoken');
const cors = require('cors');
const bodyParser = require('body-parser');
const path = require('path');

const app = express();
const PORT = 3000;
const JWT_SECRET = 'your-secret-key-change-this-in-production'; // Change this!

// Middleware
app.use(cors());
app.use(bodyParser.json());
app.use(express.static('public'));

// Initialize database
const db = new sqlite3.Database('users.db');

db.serialize(() => {
    db.run(`CREATE TABLE IF NOT EXISTS users (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        username TEXT UNIQUE NOT NULL,
        password_hash TEXT NOT NULL,
        created_at DATETIME DEFAULT CURRENT_TIMESTAMP
    )`, (err) => {
        if (err) {
            console.error('Error creating users table:', err);
        } else {
            console.log('Users table ready');
        }
    });
    
    db.run(`CREATE TABLE IF NOT EXISTS avatars (
        user_id INTEGER PRIMARY KEY,
        head_color_r REAL DEFAULT 0.8,
        head_color_g REAL DEFAULT 0.6,
        head_color_b REAL DEFAULT 0.4,
        torso_color_r REAL DEFAULT 0.2,
        torso_color_g REAL DEFAULT 0.4,
        torso_color_b REAL DEFAULT 0.8,
        left_arm_color_r REAL DEFAULT 0.8,
        left_arm_color_g REAL DEFAULT 0.6,
        left_arm_color_b REAL DEFAULT 0.4,
        right_arm_color_r REAL DEFAULT 0.8,
        right_arm_color_g REAL DEFAULT 0.6,
        right_arm_color_b REAL DEFAULT 0.4,
        left_leg_color_r REAL DEFAULT 0.2,
        left_leg_color_g REAL DEFAULT 0.6,
        left_leg_color_b REAL DEFAULT 0.2,
        right_leg_color_r REAL DEFAULT 0.2,
        right_leg_color_g REAL DEFAULT 0.6,
        right_leg_color_b REAL DEFAULT 0.2,
        FOREIGN KEY (user_id) REFERENCES users(id)
    )`, (err) => {
        if (err) {
            console.error('Error creating avatars table:', err);
        } else {
            console.log('Avatars table ready');
        }
    });
});

// Helper function to generate JWT token
function generateToken(userId, username) {
    return jwt.sign({ userId, username }, JWT_SECRET, { expiresIn: '7d' });
}

// API Routes

// Sign up
app.post('/api/signup', async (req, res) => {
    const { username, password } = req.body;
    
    if (!username || !password) {
        return res.status(400).json({ error: 'Username and password are required' });
    }
    
    if (username.length < 3 || username.length > 20) {
        return res.status(400).json({ error: 'Username must be between 3 and 20 characters' });
    }
    
    if (password.length < 6) {
        return res.status(400).json({ error: 'Password must be at least 6 characters' });
    }
    
    try {
        const hashedPassword = await bcrypt.hash(password, 10);
        
        db.run('INSERT INTO users (username, password_hash) VALUES (?, ?)', 
            [username, hashedPassword], 
            function(err) {
                if (err) {
                    if (err.message.includes('UNIQUE')) {
                        return res.status(400).json({ error: 'Username already exists' });
                    }
                    return res.status(500).json({ error: 'Database error' });
                }
                
                const token = generateToken(this.lastID, username);
                res.json({ 
                    success: true, 
                    token,
                    userId: this.lastID,
                    username 
                });
            }
        );
    } catch (error) {
        res.status(500).json({ error: 'Server error' });
    }
});

// Login
app.post('/api/login', async (req, res) => {
    const { username, password } = req.body;
    
    if (!username || !password) {
        return res.status(400).json({ error: 'Username and password are required' });
    }
    
    db.get('SELECT * FROM users WHERE username = ?', [username], async (err, row) => {
        if (err) {
            return res.status(500).json({ error: 'Database error' });
        }
        
        if (!row) {
            return res.status(401).json({ error: 'Invalid username or password' });
        }
        
        const validPassword = await bcrypt.compare(password, row.password_hash);
        if (!validPassword) {
            return res.status(401).json({ error: 'Invalid username or password' });
        }
        
        const token = generateToken(row.id, row.username);
        res.json({ 
            success: true, 
            token,
            userId: row.id,
            username: row.username 
        });
    });
});

// Verify token
app.post('/api/verify', (req, res) => {
    const { token } = req.body;
    
    if (!token) {
        return res.status(400).json({ error: 'Token is required' });
    }
    
    try {
        const decoded = jwt.verify(token, JWT_SECRET);
        res.json({ 
            success: true, 
            userId: decoded.userId,
            username: decoded.username 
        });
    } catch (error) {
        res.status(401).json({ error: 'Invalid or expired token' });
    }
});

// Get avatar
app.get('/api/avatar', (req, res) => {
    const token = req.headers.authorization?.replace('Bearer ', '') || req.query.token;
    
    if (!token) {
        return res.status(400).json({ error: 'Token is required' });
    }
    
    try {
        const decoded = jwt.verify(token, JWT_SECRET);
        const userId = decoded.userId;
        
        db.get('SELECT * FROM avatars WHERE user_id = ?', [userId], (err, row) => {
            if (err) {
                return res.status(500).json({ error: 'Database error' });
            }
            
            if (row) {
                res.json({
                    success: true,
                    avatar: {
                        headColor: [row.head_color_r, row.head_color_g, row.head_color_b],
                        torsoColor: [row.torso_color_r, row.torso_color_g, row.torso_color_b],
                        leftArmColor: [row.left_arm_color_r, row.left_arm_color_g, row.left_arm_color_b],
                        rightArmColor: [row.right_arm_color_r, row.right_arm_color_g, row.right_arm_color_b],
                        leftLegColor: [row.left_leg_color_r, row.left_leg_color_g, row.left_leg_color_b],
                        rightLegColor: [row.right_leg_color_r, row.right_leg_color_g, row.right_leg_color_b]
                    }
                });
            } else {
                // Return default avatar
                res.json({
                    success: true,
                    avatar: {
                        headColor: [0.8, 0.6, 0.4],
                        torsoColor: [0.2, 0.4, 0.8],
                        leftArmColor: [0.8, 0.6, 0.4],
                        rightArmColor: [0.8, 0.6, 0.4],
                        leftLegColor: [0.2, 0.6, 0.2],
                        rightLegColor: [0.2, 0.6, 0.2]
                    }
                });
            }
        });
    } catch (error) {
        res.status(401).json({ error: 'Invalid or expired token' });
    }
});

// Save avatar
app.post('/api/avatar', (req, res) => {
    const token = req.headers.authorization?.replace('Bearer ', '') || req.body.token;
    const { avatar } = req.body;
    
    if (!token) {
        return res.status(400).json({ error: 'Token is required' });
    }
    
    if (!avatar) {
        return res.status(400).json({ error: 'Avatar data is required' });
    }
    
    try {
        const decoded = jwt.verify(token, JWT_SECRET);
        const userId = decoded.userId;
        
        // First try to update, if no rows affected, insert
        db.run(`UPDATE avatars SET
                head_color_r = ?, head_color_g = ?, head_color_b = ?,
                torso_color_r = ?, torso_color_g = ?, torso_color_b = ?,
                left_arm_color_r = ?, left_arm_color_g = ?, left_arm_color_b = ?,
                right_arm_color_r = ?, right_arm_color_g = ?, right_arm_color_b = ?,
                left_leg_color_r = ?, left_leg_color_g = ?, left_leg_color_b = ?,
                right_leg_color_r = ?, right_leg_color_g = ?, right_leg_color_b = ?
                WHERE user_id = ?`,
            [
                avatar.headColor[0] || 0.8, avatar.headColor[1] || 0.6, avatar.headColor[2] || 0.4,
                avatar.torsoColor[0] || 0.2, avatar.torsoColor[1] || 0.4, avatar.torsoColor[2] || 0.8,
                avatar.leftArmColor[0] || 0.8, avatar.leftArmColor[1] || 0.6, avatar.leftArmColor[2] || 0.4,
                avatar.rightArmColor[0] || 0.8, avatar.rightArmColor[1] || 0.6, avatar.rightArmColor[2] || 0.4,
                avatar.leftLegColor[0] || 0.2, avatar.leftLegColor[1] || 0.6, avatar.leftLegColor[2] || 0.2,
                avatar.rightLegColor[0] || 0.2, avatar.rightLegColor[1] || 0.6, avatar.rightLegColor[2] || 0.2,
                userId
            ],
            function(err) {
                if (err) {
                    console.error('Error updating avatar:', err);
                    return res.status(500).json({ error: 'Database error: ' + err.message });
                }
                
                // If no rows were updated, insert a new record
                if (this.changes === 0) {
                    const insertValues = [
                        userId,
                        avatar.headColor && avatar.headColor[0] !== undefined ? avatar.headColor[0] : 0.8,
                        avatar.headColor && avatar.headColor[1] !== undefined ? avatar.headColor[1] : 0.6,
                        avatar.headColor && avatar.headColor[2] !== undefined ? avatar.headColor[2] : 0.4,
                        avatar.torsoColor && avatar.torsoColor[0] !== undefined ? avatar.torsoColor[0] : 0.2,
                        avatar.torsoColor && avatar.torsoColor[1] !== undefined ? avatar.torsoColor[1] : 0.4,
                        avatar.torsoColor && avatar.torsoColor[2] !== undefined ? avatar.torsoColor[2] : 0.8,
                        avatar.leftArmColor && avatar.leftArmColor[0] !== undefined ? avatar.leftArmColor[0] : 0.8,
                        avatar.leftArmColor && avatar.leftArmColor[1] !== undefined ? avatar.leftArmColor[1] : 0.6,
                        avatar.leftArmColor && avatar.leftArmColor[2] !== undefined ? avatar.leftArmColor[2] : 0.4,
                        avatar.rightArmColor && avatar.rightArmColor[0] !== undefined ? avatar.rightArmColor[0] : 0.8,
                        avatar.rightArmColor && avatar.rightArmColor[1] !== undefined ? avatar.rightArmColor[1] : 0.6,
                        avatar.rightArmColor && avatar.rightArmColor[2] !== undefined ? avatar.rightArmColor[2] : 0.4,
                        avatar.leftLegColor && avatar.leftLegColor[0] !== undefined ? avatar.leftLegColor[0] : 0.2,
                        avatar.leftLegColor && avatar.leftLegColor[1] !== undefined ? avatar.leftLegColor[1] : 0.6,
                        avatar.leftLegColor && avatar.leftLegColor[2] !== undefined ? avatar.leftLegColor[2] : 0.2,
                        avatar.rightLegColor && avatar.rightLegColor[0] !== undefined ? avatar.rightLegColor[0] : 0.2,
                        avatar.rightLegColor && avatar.rightLegColor[1] !== undefined ? avatar.rightLegColor[1] : 0.6,
                        avatar.rightLegColor && avatar.rightLegColor[2] !== undefined ? avatar.rightLegColor[2] : 0.2
                    ];
                    
                    console.log('Inserting avatar with', insertValues.length, 'values');
                    
                    db.run(`INSERT INTO avatars 
                            (user_id, head_color_r, head_color_g, head_color_b,
                             torso_color_r, torso_color_g, torso_color_b,
                             left_arm_color_r, left_arm_color_g, left_arm_color_b,
                             right_arm_color_r, right_arm_color_g, right_arm_color_b,
                             left_leg_color_r, left_leg_color_g, left_leg_color_b,
                             right_leg_color_r, right_leg_color_g, right_leg_color_b)
                            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
                        insertValues,
                        function(insertErr) {
                            if (insertErr) {
                                console.error('Error inserting avatar:', insertErr);
                                return res.status(500).json({ error: 'Database error: ' + insertErr.message });
                            }
                            res.json({ success: true });
                        }
                    );
                } else {
                    res.json({ success: true });
                }
            }
        );
    } catch (error) {
        res.status(401).json({ error: 'Invalid or expired token' });
    }
});

// Serve HTML pages
app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'public', 'index.html'));
});

app.get('/login', (req, res) => {
    res.sendFile(path.join(__dirname, 'public', 'login.html'));
});

app.get('/signup', (req, res) => {
    res.sendFile(path.join(__dirname, 'public', 'signup.html'));
});

app.get('/dashboard', (req, res) => {
    res.sendFile(path.join(__dirname, 'public', 'dashboard.html'));
});

app.get('/avatar', (req, res) => {
    res.sendFile(path.join(__dirname, 'public', 'avatar.html'));
});

app.listen(PORT, () => {
    console.log(`Website server running on http://localhost:${PORT}`);
});

