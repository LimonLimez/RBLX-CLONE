# RBLX Game Engine - Web Server

This is the authentication web server for the RBLX Game Engine.

## Setup

1. **Install Node.js** (if not already installed)
   - Download from https://nodejs.org/
   - Version 16 or higher recommended

2. **Install Dependencies**
   ```bash
   cd web
   npm install
   ```

3. **Start the Server**
   ```bash
   npm start
   ```
   
   The server will run on `http://localhost:3000`

## Features

- User signup with username and password
- User login with authentication tokens
- Token verification API
- SQLite database for user storage
- Password hashing with bcrypt
- JWT token generation

## API Endpoints

- `POST /api/signup` - Create a new account
  - Body: `{ "username": "string", "password": "string" }`
  - Returns: `{ "success": true, "token": "jwt_token", "userId": number, "username": "string" }`

- `POST /api/login` - Login with username and password
  - Body: `{ "username": "string", "password": "string" }`
  - Returns: `{ "success": true, "token": "jwt_token", "userId": number, "username": "string" }`

- `POST /api/verify` - Verify a JWT token
  - Body: `{ "token": "jwt_token" }`
  - Returns: `{ "success": true, "userId": number, "username": "string" }`

## Pages

- `/` - Home page with login/signup links
- `/login` - Login page
- `/signup` - Sign up page

## Database

The server uses SQLite and creates a `users.db` file automatically on first run.

## Security Note

**IMPORTANT**: Change the `JWT_SECRET` in `server.js` before deploying to production!


