// Quick Dark Mode Extension - content.js
// Adds a floating button to toggle dark mode on any website

(function() {
    'use strict';
    
    // Check if already initialized
    if (document.getElementById('ultralight-dark-mode-toggle')) return;
    
    // Storage key for dark mode state per domain
    const storageKey = 'ultralight-dark-mode-' + window.location.hostname;
    let isDarkMode = localStorage.getItem(storageKey) === 'true';
    
    // Create toggle button
    const button = document.createElement('button');
    button.id = 'ultralight-dark-mode-toggle';
    button.title = 'Toggle Dark Mode';
    button.innerHTML = isDarkMode ? '☀️' : '🌙';
    button.style.cssText = `
        position: fixed;
        bottom: 80px;
        right: 20px;
        width: 48px;
        height: 48px;
        border-radius: 50%;
        background: ${isDarkMode ? '#fbbf24' : '#1e293b'};
        border: none;
        cursor: pointer;
        font-size: 24px;
        box-shadow: 0 4px 15px rgba(0,0,0,0.3);
        z-index: 999998;
        transition: all 0.3s ease;
        display: flex;
        align-items: center;
        justify-content: center;
    `;
    
    // Create dark mode style element
    const darkModeStyle = document.createElement('style');
    darkModeStyle.id = 'ultralight-dark-mode-style';
    darkModeStyle.textContent = `
        html.ultralight-dark-mode {
            filter: invert(1) hue-rotate(180deg);
        }
        html.ultralight-dark-mode img,
        html.ultralight-dark-mode video,
        html.ultralight-dark-mode picture,
        html.ultralight-dark-mode canvas,
        html.ultralight-dark-mode svg,
        html.ultralight-dark-mode [style*="background-image"],
        html.ultralight-dark-mode iframe {
            filter: invert(1) hue-rotate(180deg);
        }
        html.ultralight-dark-mode #ultralight-dark-mode-toggle {
            filter: invert(1) hue-rotate(180deg);
        }
    `;
    document.head.appendChild(darkModeStyle);
    
    // Apply initial state
    if (isDarkMode) {
        document.documentElement.classList.add('ultralight-dark-mode');
    }
    
    // Toggle handler
    button.addEventListener('click', () => {
        isDarkMode = !isDarkMode;
        localStorage.setItem(storageKey, isDarkMode);
        
        if (isDarkMode) {
            document.documentElement.classList.add('ultralight-dark-mode');
            button.innerHTML = '☀️';
            button.style.background = '#fbbf24';
        } else {
            document.documentElement.classList.remove('ultralight-dark-mode');
            button.innerHTML = '🌙';
            button.style.background = '#1e293b';
        }
        
        // Animation
        button.style.transform = 'scale(1.2)';
        setTimeout(() => {
            button.style.transform = 'scale(1)';
        }, 150);
    });
    
    // Hover effect
    button.addEventListener('mouseenter', () => {
        button.style.transform = 'scale(1.1)';
    });
    button.addEventListener('mouseleave', () => {
        button.style.transform = 'scale(1)';
    });
    
    // Add to page
    document.body.appendChild(button);
    
    console.log('%c[Quick Dark Mode] Loaded for ' + window.location.hostname, 
        'color: #1e293b; font-weight: bold; font-size: 12px;');
})();
