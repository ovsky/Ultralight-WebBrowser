// Hello World Extension - content.js
// This script runs on every page that matches the pattern in manifest.json

(function() {
    'use strict';
    
    // Create a small notification badge
    const badge = document.createElement('div');
    badge.id = 'ultralight-hello-world-badge';
    badge.innerHTML = `
        <div style="
            position: fixed;
            bottom: 20px;
            right: 20px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 12px 20px;
            border-radius: 8px;
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            font-size: 14px;
            box-shadow: 0 4px 15px rgba(102, 126, 234, 0.4);
            z-index: 999999;
            display: flex;
            align-items: center;
            gap: 10px;
            cursor: pointer;
            transition: all 0.3s ease;
            opacity: 0;
            transform: translateY(20px);
        ">
            <span style="font-size: 20px;">👋</span>
            <span>Hello from Ultralight Extension!</span>
            <button style="
                background: rgba(255,255,255,0.2);
                border: none;
                color: white;
                width: 20px;
                height: 20px;
                border-radius: 50%;
                cursor: pointer;
                font-size: 12px;
                display: flex;
                align-items: center;
                justify-content: center;
                margin-left: 8px;
            ">×</button>
        </div>
    `;
    
    // Add to page
    document.body.appendChild(badge);
    
    // Animate in
    const inner = badge.firstElementChild;
    setTimeout(() => {
        inner.style.opacity = '1';
        inner.style.transform = 'translateY(0)';
    }, 100);
    
    // Close button handler
    const closeBtn = badge.querySelector('button');
    closeBtn.addEventListener('click', (e) => {
        e.stopPropagation();
        inner.style.opacity = '0';
        inner.style.transform = 'translateY(20px)';
        setTimeout(() => badge.remove(), 300);
    });
    
    // Auto-hide after 5 seconds
    setTimeout(() => {
        if (document.body.contains(badge)) {
            inner.style.opacity = '0';
            inner.style.transform = 'translateY(20px)';
            setTimeout(() => badge.remove(), 300);
        }
    }, 5000);
    
    // Log to console
    console.log('%c[Hello World Extension] Loaded successfully!', 
        'color: #667eea; font-weight: bold; font-size: 12px;');
})();
