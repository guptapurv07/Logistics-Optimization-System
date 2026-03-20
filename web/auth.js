const UserDatabase = {
    // Simulated CSV Data Import
    init() {
        if (!localStorage.getItem('users')) {
            const initialUsers = [
                // warehouse_manager.csv data
                { username: 'admin', password: '123', role: 'manager', id: 'MGR-001' },
                { username: 'sarah_logistics', password: 'password', role: 'manager', id: 'MGR-002' },
                // truck_driver.csv data
                { username: 'driver1', password: '123', role: 'driver', id: 'DL-9901' },
                { username: 'john_doe', password: 'password', role: 'driver', id: 'DL-4421' }
            ];
            localStorage.setItem('users', JSON.stringify(initialUsers));
            console.log("Database Sync: CSV records loaded into LocalStorage.");
        }
    },

    signUp(username, password, role) {
        let users = JSON.parse(localStorage.getItem('users')) || [];
        if (users.find(u => u.username === username)) {
            return { success: false, message: "User already exists in database!" };
        }
        users.push({ username, password, role, id: 'NEW-' + Math.floor(Math.random()*1000) });
        localStorage.setItem('users', JSON.stringify(users));
        return { success: true, message: "Sign-up successful! Your data is now in the database." };
    },

    login(username, password) {
        const users = JSON.parse(localStorage.getItem('users')) || [];
        const user = users.find(u => u.username === username && u.password === password);
        if (user) {
            return { success: true, role: user.role, name: user.username };
        }
        return { success: false, message: "Invalid credentials. Access denied." };
    }
};

UserDatabase.init();