/**
 * Theme Manager JavaScript
 * ========================
 * Handles theme loading, applying, and management for Ultralight WebBrowser
 */

(function() {
    'use strict';

    // Theme storage key
    const THEME_STORAGE_KEY = 'ultralight_active_theme';
    const CUSTOM_THEMES_KEY = 'ultralight_custom_themes';

    // Default themes that ship with the browser
    // Note: Organized with more dark themes as they are more popular
    const DEFAULT_THEMES = {
        // =================================================================
        // DARK THEMES
        // =================================================================
        'dark': {
            id: 'dark',
            name: 'Dark (Default)',
            description: 'The default dark purple theme',
            author: 'Ultralight Team',
            version: '1.0.0',
            isBuiltIn: true,
            category: 'dark',
            colors: {
                // Background Colors
                'color-bg-primary': '#16151d',
                'color-bg-secondary': '#1e1e2e',
                'color-bg-tertiary': '#232330',
                'color-bg-elevated': '#282839',
                'color-bg-hover': '#343446',
                'color-bg-active': '#3d3d5c',
                'color-bg-overlay': 'rgba(22, 21, 29, 0.95)',

                // Text Colors
                'color-text-primary': '#e4e4ef',
                'color-text-secondary': '#c4c2d0',
                'color-text-tertiary': '#9999b3',
                'color-text-muted': '#71718a',

                // Border Colors
                'color-border-primary': '#313146',
                'color-border-secondary': '#252532',

                // Accent Colors
                'color-accent-primary': '#6C63FF',
                'color-accent-secondary': '#7c6aef',
                'color-accent-hover': '#8a83ff',

                // Status Colors
                'color-success': '#6aef8a',
                'color-warning': '#f0b866',
                'color-danger': '#ef6a6a',
                'color-info': '#6ac0ef',

                // Navbar/Toolbar - COMPLETE SET
                'toolbar-bg': 'linear-gradient(0deg, #232330, #282836)',
                'toolbar-border': '#252532',
                'toolbar-icon-color': '#c8c8c8',
                'toolbar-icon-hover': '#ffffff',
                'toolbar-icon-active': '#6C63FF',
                'toolbar-icon-disabled': '#636074',
                'toolbar-icon-hover-bg': '#343446',

                // Address Bar
                'address-bg': '#16151d',
                'address-text': '#c4c2d0',
                'address-focus-text': '#ffffff',
                'address-focus-ring': '#343446',

                // Tabs
                'tab-bg': 'linear-gradient(180deg, #212130, #1c1c24)',
                'tab-active-bg': 'linear-gradient(180deg, #313141, #282836)',
                'tab-hover-bg': 'linear-gradient(180deg, #2d2d3d, #323242)',
                'tab-text': '#b0afc0',
                'tab-active-text': '#e0dff0',

                // Menu & Cards
                'menu-bg': '#2b2b38',
                'menu-border': '#313146',
                'menu-item-hover': '#343446',
                'menu-text': '#e2e1ea',
                'menu-separator': '#3a3a4e',
                'card-bg': '#282839',

                // Inputs
                'input-bg': '#32324a',
                'input-border': '#404060',

                // Scrollbar
                'scrollbar-track': '#1e1e2e',
                'scrollbar-thumb': '#3d3d5c',

                // Tooltip
                'tooltip-bg': 'rgba(43, 43, 56, 0.95)',
                'tooltip-text': '#f0f0ff',

                // Buttons
                'btn-primary-bg': '#6C63FF',
                'btn-primary-hover': '#7a72ff',
                'btn-secondary-bg': 'rgba(255, 255, 255, 0.1)',
                'btn-secondary-hover': 'rgba(255, 255, 255, 0.15)',

                // Session Restore Bar
                'session-restore-bg': 'linear-gradient(180deg, #3d3d5c 0%, #2d2d44 100%)',
                'session-restore-border': '#4a4a6a',
                'session-restore-text': '#e4e4ef',
                'session-restore-icon': '#9090b0',

                // DRM Prompt Bar
                'drm-bar-bg': 'linear-gradient(135deg, #5a4fcf, #7c6aef)',
                'drm-bar-text': '#ffffff',
                'drm-btn-bg': 'rgba(255, 255, 255, 0.15)',
                'drm-btn-hover': 'rgba(255, 255, 255, 0.25)',
                'drm-btn-primary-bg': '#ffffff',
                'drm-btn-primary-text': '#5a4fcf',

                // Radius (in pixels)
                'radius-sm': '4',
                'radius-md': '8',
                'radius-lg': '12'
            }
        },
        'midnight': {
            id: 'midnight',
            name: 'Midnight Blue',
            description: 'Deep blue night theme inspired by GitHub Dark',
            author: 'Ultralight Team',
            version: '1.0.0',
            isBuiltIn: true,
            category: 'dark',
            colors: {
                'color-bg-primary': '#0d1117',
                'color-bg-secondary': '#161b22',
                'color-bg-tertiary': '#21262d',
                'color-bg-elevated': '#30363d',
                'color-bg-hover': '#3d444d',
                'color-bg-active': '#484f58',
                'color-bg-overlay': 'rgba(13, 17, 23, 0.95)',

                'color-text-primary': '#e6edf3',
                'color-text-secondary': '#c9d1d9',
                'color-text-tertiary': '#8b949e',
                'color-text-muted': '#6e7681',

                'color-border-primary': '#30363d',
                'color-border-secondary': '#21262d',

                'color-accent-primary': '#58a6ff',
                'color-accent-secondary': '#79c0ff',
                'color-accent-hover': '#a5d6ff',

                'color-success': '#3fb950',
                'color-warning': '#d29922',
                'color-danger': '#f85149',
                'color-info': '#58a6ff',

                'toolbar-bg': 'linear-gradient(0deg, #161b22, #21262d)',
                'toolbar-border': '#21262d',
                'toolbar-icon-color': '#8b949e',
                'toolbar-icon-hover': '#e6edf3',
                'toolbar-icon-active': '#58a6ff',
                'toolbar-icon-disabled': '#484f58',
                'toolbar-icon-hover-bg': '#30363d',

                'address-bg': '#0d1117',
                'address-text': '#c9d1d9',
                'address-focus-text': '#e6edf3',
                'address-focus-ring': '#30363d',

                'tab-bg': 'linear-gradient(180deg, #161b22, #0d1117)',
                'tab-active-bg': 'linear-gradient(180deg, #21262d, #161b22)',
                'tab-hover-bg': 'linear-gradient(180deg, #1c232c, #161b22)',
                'tab-text': '#8b949e',
                'tab-active-text': '#e6edf3',

                'menu-bg': '#21262d',
                'menu-border': '#30363d',
                'menu-item-hover': '#30363d',
                'menu-text': '#c9d1d9',
                'menu-separator': '#30363d',
                'card-bg': '#21262d',

                'input-bg': '#0d1117',
                'input-border': '#30363d',

                'scrollbar-track': '#0d1117',
                'scrollbar-thumb': '#30363d',

                'tooltip-bg': 'rgba(33, 38, 45, 0.95)',
                'tooltip-text': '#e6edf3',

                'btn-primary-bg': '#238636',
                'btn-primary-hover': '#2ea043',
                'btn-secondary-bg': 'rgba(110, 118, 129, 0.2)',
                'btn-secondary-hover': 'rgba(110, 118, 129, 0.3)',

                'radius-sm': '6',
                'radius-md': '8',
                'radius-lg': '12'
            }
        },
        'dracula': {
            id: 'dracula',
            name: 'Dracula',
            description: 'A dark theme with vibrant colors',
            author: 'Ultralight Team',
            version: '1.0.0',
            isBuiltIn: true,
            category: 'dark',
            colors: {
                'color-bg-primary': '#282a36',
                'color-bg-secondary': '#21222c',
                'color-bg-tertiary': '#343746',
                'color-bg-elevated': '#3d3f4d',
                'color-bg-hover': '#44475a',
                'color-bg-active': '#4d4f5e',
                'color-bg-overlay': 'rgba(40, 42, 54, 0.95)',

                'color-text-primary': '#f8f8f2',
                'color-text-secondary': '#d4d4d4',
                'color-text-tertiary': '#a9a9b3',
                'color-text-muted': '#6272a4',

                'color-border-primary': '#44475a',
                'color-border-secondary': '#343746',

                'color-accent-primary': '#bd93f9',
                'color-accent-secondary': '#ff79c6',
                'color-accent-hover': '#caa7fa',

                'color-success': '#50fa7b',
                'color-warning': '#f1fa8c',
                'color-danger': '#ff5555',
                'color-info': '#8be9fd',

                'toolbar-bg': 'linear-gradient(0deg, #21222c, #282a36)',
                'toolbar-icon-color': '#f8f8f2',
                'toolbar-icon-hover': '#bd93f9',
                'toolbar-icon-active': '#ff79c6',

                'tab-bg': '#21222c',
                'tab-active-bg': '#343746',
                'tab-hover-bg': '#282a36',
                'tab-text': '#6272a4',
                'tab-active-text': '#f8f8f2',

                'menu-bg': '#282a36',
                'card-bg': '#343746',

                'input-bg': '#21222c',
                'input-border': '#44475a',

                'scrollbar-track': '#21222c',
                'scrollbar-thumb': '#44475a',

                'radius-sm': '4',
                'radius-md': '8',
                'radius-lg': '12'
            }
        },
        'onedark': {
            id: 'onedark',
            name: 'One Dark',
            description: 'Atom One Dark inspired theme',
            author: 'Ultralight Team',
            version: '1.0.0',
            isBuiltIn: true,
            category: 'dark',
            colors: {
                'color-bg-primary': '#282c34',
                'color-bg-secondary': '#21252b',
                'color-bg-tertiary': '#2c323c',
                'color-bg-elevated': '#353b45',
                'color-bg-hover': '#3e4451',
                'color-bg-active': '#4d5566',
                'color-bg-overlay': 'rgba(40, 44, 52, 0.95)',

                'color-text-primary': '#abb2bf',
                'color-text-secondary': '#9da5b4',
                'color-text-tertiary': '#7f848e',
                'color-text-muted': '#5c6370',

                'color-border-primary': '#3e4451',
                'color-border-secondary': '#2c323c',

                'color-accent-primary': '#61afef',
                'color-accent-secondary': '#c678dd',
                'color-accent-hover': '#7ec8f3',

                'color-success': '#98c379',
                'color-warning': '#e5c07b',
                'color-danger': '#e06c75',
                'color-info': '#61afef',

                'toolbar-bg': 'linear-gradient(0deg, #21252b, #282c34)',
                'toolbar-icon-color': '#9da5b4',
                'toolbar-icon-hover': '#abb2bf',
                'toolbar-icon-active': '#61afef',

                'tab-bg': '#21252b',
                'tab-active-bg': '#282c34',
                'tab-hover-bg': '#2c323c',
                'tab-text': '#5c6370',
                'tab-active-text': '#abb2bf',

                'menu-bg': '#2c323c',
                'card-bg': '#2c323c',

                'input-bg': '#21252b',
                'input-border': '#3e4451',

                'scrollbar-track': '#21252b',
                'scrollbar-thumb': '#3e4451',

                'radius-sm': '4',
                'radius-md': '6',
                'radius-lg': '10'
            }
        },
        'gruvbox': {
            id: 'gruvbox',
            name: 'Gruvbox Dark',
            description: 'Retro groove color scheme',
            author: 'Ultralight Team',
            version: '1.0.0',
            isBuiltIn: true,
            category: 'dark',
            colors: {
                'color-bg-primary': '#282828',
                'color-bg-secondary': '#1d2021',
                'color-bg-tertiary': '#32302f',
                'color-bg-elevated': '#3c3836',
                'color-bg-hover': '#504945',
                'color-bg-active': '#665c54',
                'color-bg-overlay': 'rgba(40, 40, 40, 0.95)',

                'color-text-primary': '#ebdbb2',
                'color-text-secondary': '#d5c4a1',
                'color-text-tertiary': '#bdae93',
                'color-text-muted': '#928374',

                'color-border-primary': '#504945',
                'color-border-secondary': '#3c3836',

                'color-accent-primary': '#fe8019',
                'color-accent-secondary': '#fabd2f',
                'color-accent-hover': '#ff9633',

                'color-success': '#b8bb26',
                'color-warning': '#fabd2f',
                'color-danger': '#fb4934',
                'color-info': '#83a598',

                'toolbar-bg': 'linear-gradient(0deg, #1d2021, #282828)',
                'toolbar-icon-color': '#d5c4a1',
                'toolbar-icon-hover': '#ebdbb2',
                'toolbar-icon-active': '#fe8019',

                'tab-bg': '#1d2021',
                'tab-active-bg': '#32302f',
                'tab-hover-bg': '#282828',
                'tab-text': '#928374',
                'tab-active-text': '#ebdbb2',

                'menu-bg': '#32302f',
                'card-bg': '#3c3836',

                'input-bg': '#1d2021',
                'input-border': '#504945',

                'scrollbar-track': '#1d2021',
                'scrollbar-thumb': '#504945',

                'radius-sm': '2',
                'radius-md': '4',
                'radius-lg': '8'
            }
        },
        'catppuccin': {
            id: 'catppuccin',
            name: 'Catppuccin Mocha',
            description: 'Soothing pastel theme for the high-spirited',
            author: 'Ultralight Team',
            version: '1.0.0',
            isBuiltIn: true,
            category: 'dark',
            colors: {
                'color-bg-primary': '#1e1e2e',
                'color-bg-secondary': '#181825',
                'color-bg-tertiary': '#313244',
                'color-bg-elevated': '#45475a',
                'color-bg-hover': '#585b70',
                'color-bg-active': '#6c7086',
                'color-bg-overlay': 'rgba(30, 30, 46, 0.95)',

                'color-text-primary': '#cdd6f4',
                'color-text-secondary': '#bac2de',
                'color-text-tertiary': '#a6adc8',
                'color-text-muted': '#6c7086',

                'color-border-primary': '#45475a',
                'color-border-secondary': '#313244',

                'color-accent-primary': '#cba6f7',
                'color-accent-secondary': '#f5c2e7',
                'color-accent-hover': '#dbb9f9',

                'color-success': '#a6e3a1',
                'color-warning': '#f9e2af',
                'color-danger': '#f38ba8',
                'color-info': '#89dceb',

                'toolbar-bg': 'linear-gradient(0deg, #181825, #1e1e2e)',
                'toolbar-icon-color': '#bac2de',
                'toolbar-icon-hover': '#cdd6f4',
                'toolbar-icon-active': '#cba6f7',

                'tab-bg': '#181825',
                'tab-active-bg': '#313244',
                'tab-hover-bg': '#1e1e2e',
                'tab-text': '#6c7086',
                'tab-active-text': '#cdd6f4',

                'menu-bg': '#313244',
                'card-bg': '#313244',

                'input-bg': '#181825',
                'input-border': '#45475a',

                'scrollbar-track': '#181825',
                'scrollbar-thumb': '#45475a',

                'radius-sm': '6',
                'radius-md': '10',
                'radius-lg': '14'
            }
        },
        'tokyo-night': {
            id: 'tokyo-night',
            name: 'Tokyo Night',
            description: 'A clean dark theme celebrating the lights of Tokyo',
            author: 'Ultralight Team',
            version: '1.0.0',
            isBuiltIn: true,
            category: 'dark',
            colors: {
                'color-bg-primary': '#1a1b26',
                'color-bg-secondary': '#16161e',
                'color-bg-tertiary': '#24283b',
                'color-bg-elevated': '#292e42',
                'color-bg-hover': '#343b58',
                'color-bg-active': '#414868',
                'color-bg-overlay': 'rgba(26, 27, 38, 0.95)',

                'color-text-primary': '#c0caf5',
                'color-text-secondary': '#a9b1d6',
                'color-text-tertiary': '#9aa5ce',
                'color-text-muted': '#565f89',

                'color-border-primary': '#343b58',
                'color-border-secondary': '#24283b',

                'color-accent-primary': '#7aa2f7',
                'color-accent-secondary': '#bb9af7',
                'color-accent-hover': '#9bb5f9',

                'color-success': '#9ece6a',
                'color-warning': '#e0af68',
                'color-danger': '#f7768e',
                'color-info': '#7dcfff',

                'toolbar-bg': 'linear-gradient(0deg, #16161e, #1a1b26)',
                'toolbar-icon-color': '#a9b1d6',
                'toolbar-icon-hover': '#c0caf5',
                'toolbar-icon-active': '#7aa2f7',

                'tab-bg': '#16161e',
                'tab-active-bg': '#24283b',
                'tab-hover-bg': '#1a1b26',
                'tab-text': '#565f89',
                'tab-active-text': '#c0caf5',

                'menu-bg': '#24283b',
                'card-bg': '#24283b',

                'input-bg': '#16161e',
                'input-border': '#343b58',

                'scrollbar-track': '#16161e',
                'scrollbar-thumb': '#343b58',

                'radius-sm': '4',
                'radius-md': '8',
                'radius-lg': '12'
            }
        },
        'ayu-dark': {
            id: 'ayu-dark',
            name: 'Ayu Dark',
            description: 'Modern and minimal dark theme',
            author: 'Ultralight Team',
            version: '1.0.0',
            isBuiltIn: true,
            category: 'dark',
            colors: {
                'color-bg-primary': '#0d1017',
                'color-bg-secondary': '#0a0e14',
                'color-bg-tertiary': '#131721',
                'color-bg-elevated': '#1a1f29',
                'color-bg-hover': '#242936',
                'color-bg-active': '#2d3340',
                'color-bg-overlay': 'rgba(13, 16, 23, 0.95)',

                'color-text-primary': '#bfbdb6',
                'color-text-secondary': '#a09d96',
                'color-text-tertiary': '#73726e',
                'color-text-muted': '#565451',

                'color-border-primary': '#1a1f29',
                'color-border-secondary': '#131721',

                'color-accent-primary': '#ffb454',
                'color-accent-secondary': '#e6b450',
                'color-accent-hover': '#ffcc7d',

                'color-success': '#aad94c',
                'color-warning': '#ffb454',
                'color-danger': '#f07178',
                'color-info': '#39bae6',

                'toolbar-bg': 'linear-gradient(0deg, #0a0e14, #0d1017)',
                'toolbar-icon-color': '#a09d96',
                'toolbar-icon-hover': '#bfbdb6',
                'toolbar-icon-active': '#ffb454',

                'tab-bg': '#0a0e14',
                'tab-active-bg': '#131721',
                'tab-hover-bg': '#0d1017',
                'tab-text': '#565451',
                'tab-active-text': '#bfbdb6',

                'menu-bg': '#131721',
                'card-bg': '#1a1f29',

                'input-bg': '#0a0e14',
                'input-border': '#1a1f29',

                'scrollbar-track': '#0a0e14',
                'scrollbar-thumb': '#1a1f29',

                'radius-sm': '4',
                'radius-md': '6',
                'radius-lg': '10'
            }
        },
        'solarized-dark': {
            id: 'solarized-dark',
            name: 'Solarized Dark',
            description: 'Precision color scheme for professionals',
            author: 'Ultralight Team',
            version: '1.0.0',
            isBuiltIn: true,
            category: 'dark',
            colors: {
                'color-bg-primary': '#002b36',
                'color-bg-secondary': '#001e26',
                'color-bg-tertiary': '#073642',
                'color-bg-elevated': '#0a4352',
                'color-bg-hover': '#0d5564',
                'color-bg-active': '#106878',
                'color-bg-overlay': 'rgba(0, 43, 54, 0.95)',

                'color-text-primary': '#93a1a1',
                'color-text-secondary': '#839496',
                'color-text-tertiary': '#657b83',
                'color-text-muted': '#586e75',

                'color-border-primary': '#073642',
                'color-border-secondary': '#002b36',

                'color-accent-primary': '#2aa198',
                'color-accent-secondary': '#268bd2',
                'color-accent-hover': '#45b6ad',

                'color-success': '#859900',
                'color-warning': '#b58900',
                'color-danger': '#dc322f',
                'color-info': '#268bd2',

                'toolbar-bg': 'linear-gradient(0deg, #001e26, #002b36)',
                'toolbar-icon-color': '#839496',
                'toolbar-icon-hover': '#93a1a1',
                'toolbar-icon-active': '#2aa198',

                'tab-bg': '#001e26',
                'tab-active-bg': '#073642',
                'tab-hover-bg': '#002b36',
                'tab-text': '#586e75',
                'tab-active-text': '#93a1a1',

                'menu-bg': '#073642',
                'card-bg': '#073642',

                'input-bg': '#001e26',
                'input-border': '#073642',

                'scrollbar-track': '#001e26',
                'scrollbar-thumb': '#073642',

                'radius-sm': '4',
                'radius-md': '6',
                'radius-lg': '8'
            }
        },
        'material-dark': {
            id: 'material-dark',
            name: 'Material Dark',
            description: 'Material Design inspired dark theme',
            author: 'Ultralight Team',
            version: '1.0.0',
            isBuiltIn: true,
            category: 'dark',
            colors: {
                'color-bg-primary': '#212121',
                'color-bg-secondary': '#1a1a1a',
                'color-bg-tertiary': '#2d2d2d',
                'color-bg-elevated': '#383838',
                'color-bg-hover': '#404040',
                'color-bg-active': '#4a4a4a',
                'color-bg-overlay': 'rgba(33, 33, 33, 0.95)',

                'color-text-primary': '#eeffff',
                'color-text-secondary': '#b2ccd6',
                'color-text-tertiary': '#89a0a8',
                'color-text-muted': '#546e7a',

                'color-border-primary': '#383838',
                'color-border-secondary': '#2d2d2d',

                'color-accent-primary': '#82aaff',
                'color-accent-secondary': '#c792ea',
                'color-accent-hover': '#9dc3ff',

                'color-success': '#c3e88d',
                'color-warning': '#ffcb6b',
                'color-danger': '#f07178',
                'color-info': '#89ddff',

                'toolbar-bg': 'linear-gradient(0deg, #1a1a1a, #212121)',
                'toolbar-icon-color': '#b2ccd6',
                'toolbar-icon-hover': '#eeffff',
                'toolbar-icon-active': '#82aaff',

                'tab-bg': '#1a1a1a',
                'tab-active-bg': '#2d2d2d',
                'tab-hover-bg': '#212121',
                'tab-text': '#546e7a',
                'tab-active-text': '#eeffff',

                'menu-bg': '#2d2d2d',
                'card-bg': '#2d2d2d',

                'input-bg': '#1a1a1a',
                'input-border': '#383838',

                'scrollbar-track': '#1a1a1a',
                'scrollbar-thumb': '#383838',

                'radius-sm': '4',
                'radius-md': '8',
                'radius-lg': '12'
            }
        },
        'nord': {
            id: 'nord',
            name: 'Nord',
            description: 'Arctic, north-bluish color palette',
            author: 'Ultralight Team',
            version: '1.0.0',
            isBuiltIn: true,
            category: 'dark',
            colors: {
                'color-bg-primary': '#2e3440',
                'color-bg-secondary': '#272c36',
                'color-bg-tertiary': '#3b4252',
                'color-bg-elevated': '#434c5e',
                'color-bg-hover': '#4c566a',
                'color-bg-active': '#5e6779',
                'color-bg-overlay': 'rgba(46, 52, 64, 0.95)',

                'color-text-primary': '#eceff4',
                'color-text-secondary': '#e5e9f0',
                'color-text-tertiary': '#d8dee9',
                'color-text-muted': '#a5adba',

                'color-border-primary': '#4c566a',
                'color-border-secondary': '#3b4252',

                'color-accent-primary': '#88c0d0',
                'color-accent-secondary': '#81a1c1',
                'color-accent-hover': '#8fbcbb',

                'color-success': '#a3be8c',
                'color-warning': '#ebcb8b',
                'color-danger': '#bf616a',
                'color-info': '#5e81ac',

                'toolbar-bg': 'linear-gradient(0deg, #272c36, #2e3440)',
                'toolbar-icon-color': '#d8dee9',
                'toolbar-icon-hover': '#eceff4',
                'toolbar-icon-active': '#88c0d0',

                'tab-bg': '#272c36',
                'tab-active-bg': '#3b4252',
                'tab-hover-bg': '#2e3440',
                'tab-text': '#a5adba',
                'tab-active-text': '#eceff4',

                'menu-bg': '#3b4252',
                'card-bg': '#3b4252',

                'input-bg': '#272c36',
                'input-border': '#4c566a',

                'scrollbar-track': '#272c36',
                'scrollbar-thumb': '#4c566a',

                'radius-sm': '4',
                'radius-md': '8',
                'radius-lg': '12'
            }
        },
        'monokai': {
            id: 'monokai',
            name: 'Monokai Pro',
            description: 'Classic Monokai color scheme',
            author: 'Ultralight Team',
            version: '1.0.0',
            isBuiltIn: true,
            category: 'dark',
            colors: {
                'color-bg-primary': '#2d2a2e',
                'color-bg-secondary': '#221f22',
                'color-bg-tertiary': '#353236',
                'color-bg-elevated': '#403e41',
                'color-bg-hover': '#4a474c',
                'color-bg-active': '#555158',
                'color-bg-overlay': 'rgba(45, 42, 46, 0.95)',

                'color-text-primary': '#fcfcfa',
                'color-text-secondary': '#c1c0c0',
                'color-text-tertiary': '#939293',
                'color-text-muted': '#727072',

                'color-border-primary': '#4a474c',
                'color-border-secondary': '#353236',

                'color-accent-primary': '#ffd866',
                'color-accent-secondary': '#ff6188',
                'color-accent-hover': '#ffe099',

                'color-success': '#a9dc76',
                'color-warning': '#ffd866',
                'color-danger': '#ff6188',
                'color-info': '#78dce8',

                'toolbar-bg': 'linear-gradient(0deg, #221f22, #2d2a2e)',
                'toolbar-icon-color': '#c1c0c0',
                'toolbar-icon-hover': '#fcfcfa',
                'toolbar-icon-active': '#ffd866',

                'tab-bg': '#221f22',
                'tab-active-bg': '#353236',
                'tab-hover-bg': '#2d2a2e',
                'tab-text': '#727072',
                'tab-active-text': '#fcfcfa',

                'menu-bg': '#353236',
                'card-bg': '#353236',

                'input-bg': '#221f22',
                'input-border': '#4a474c',

                'scrollbar-track': '#221f22',
                'scrollbar-thumb': '#4a474c',

                'radius-sm': '4',
                'radius-md': '8',
                'radius-lg': '12'
            }
        },
        'ocean-deep': {
            id: 'ocean-deep',
            name: 'Ocean Deep',
            description: 'Deep ocean blues for late-night browsing',
            author: 'Ultralight Team',
            version: '1.0.0',
            isBuiltIn: true,
            category: 'dark',
            colors: {
                'color-bg-primary': '#0f111a',
                'color-bg-secondary': '#090b10',
                'color-bg-tertiary': '#1a1c25',
                'color-bg-elevated': '#242630',
                'color-bg-hover': '#2e313c',
                'color-bg-active': '#383b48',
                'color-bg-overlay': 'rgba(15, 17, 26, 0.95)',

                'color-text-primary': '#a6accd',
                'color-text-secondary': '#8a90b3',
                'color-text-tertiary': '#6e7399',
                'color-text-muted': '#525880',

                'color-border-primary': '#242630',
                'color-border-secondary': '#1a1c25',

                'color-accent-primary': '#84ffff',
                'color-accent-secondary': '#80cbc4',
                'color-accent-hover': '#b2fff9',

                'color-success': '#c3e88d',
                'color-warning': '#ffcb6b',
                'color-danger': '#ff5370',
                'color-info': '#82aaff',

                'toolbar-bg': 'linear-gradient(0deg, #090b10, #0f111a)',
                'toolbar-icon-color': '#8a90b3',
                'toolbar-icon-hover': '#a6accd',
                'toolbar-icon-active': '#84ffff',

                'tab-bg': '#090b10',
                'tab-active-bg': '#1a1c25',
                'tab-hover-bg': '#0f111a',
                'tab-text': '#525880',
                'tab-active-text': '#a6accd',

                'menu-bg': '#1a1c25',
                'card-bg': '#1a1c25',

                'input-bg': '#090b10',
                'input-border': '#242630',

                'scrollbar-track': '#090b10',
                'scrollbar-thumb': '#242630',

                'radius-sm': '4',
                'radius-md': '8',
                'radius-lg': '12'
            }
        },

        // =================================================================
        // LIGHT THEMES
        // =================================================================
        'light': {
            id: 'light',
            name: 'Light',
            description: 'Clean light theme for daytime use',
            author: 'Ultralight Team',
            version: '1.0.0',
            isBuiltIn: true,
            category: 'light',
            colors: {
                'color-bg-primary': '#ffffff',
                'color-bg-secondary': '#f6f8fa',
                'color-bg-tertiary': '#eaeef2',
                'color-bg-elevated': '#ffffff',
                'color-bg-hover': '#e8ebef',
                'color-bg-active': '#dce0e4',
                'color-bg-overlay': 'rgba(255, 255, 255, 0.95)',

                'color-text-primary': '#1f2328',
                'color-text-secondary': '#424a53',
                'color-text-tertiary': '#656d76',
                'color-text-muted': '#8c959f',

                'color-border-primary': '#d0d7de',
                'color-border-secondary': '#e1e4e8',

                'color-accent-primary': '#0969da',
                'color-accent-secondary': '#218bff',
                'color-accent-hover': '#0550ae',

                'color-success': '#1a7f37',
                'color-warning': '#9a6700',
                'color-danger': '#cf222e',
                'color-info': '#0550ae',

                'toolbar-bg': 'linear-gradient(0deg, #f6f8fa, #ffffff)',
                'toolbar-icon-color': '#656d76',
                'toolbar-icon-hover': '#1f2328',
                'toolbar-icon-active': '#0969da',

                'tab-bg': '#f6f8fa',
                'tab-active-bg': '#ffffff',
                'tab-hover-bg': '#eaeef2',
                'tab-text': '#656d76',
                'tab-active-text': '#1f2328',

                'menu-bg': '#ffffff',
                'card-bg': '#ffffff',

                'input-bg': '#f6f8fa',
                'input-border': '#d0d7de',

                'scrollbar-track': '#f6f8fa',
                'scrollbar-thumb': '#d0d7de',

                'radius-sm': '6',
                'radius-md': '8',
                'radius-lg': '12'
            }
        },
        'light-soft': {
            id: 'light-soft',
            name: 'Light Soft',
            description: 'Warm, easy-on-the-eyes light theme',
            author: 'Ultralight Team',
            version: '1.0.0',
            isBuiltIn: true,
            category: 'light',
            colors: {
                'color-bg-primary': '#faf8f5',
                'color-bg-secondary': '#f5f2ed',
                'color-bg-tertiary': '#ebe8e3',
                'color-bg-elevated': '#ffffff',
                'color-bg-hover': '#e5e2dd',
                'color-bg-active': '#dbd8d3',
                'color-bg-overlay': 'rgba(250, 248, 245, 0.95)',

                'color-text-primary': '#2e2e2e',
                'color-text-secondary': '#5a5a5a',
                'color-text-tertiary': '#7a7a7a',
                'color-text-muted': '#9a9a9a',

                'color-border-primary': '#e0ddd8',
                'color-border-secondary': '#ebe8e3',

                'color-accent-primary': '#d97706',
                'color-accent-secondary': '#f59e0b',
                'color-accent-hover': '#b45309',

                'color-success': '#059669',
                'color-warning': '#d97706',
                'color-danger': '#dc2626',
                'color-info': '#0284c7',

                'toolbar-bg': 'linear-gradient(0deg, #f5f2ed, #faf8f5)',
                'toolbar-icon-color': '#7a7a7a',
                'toolbar-icon-hover': '#2e2e2e',
                'toolbar-icon-active': '#d97706',

                'tab-bg': '#f5f2ed',
                'tab-active-bg': '#faf8f5',
                'tab-hover-bg': '#ebe8e3',
                'tab-text': '#7a7a7a',
                'tab-active-text': '#2e2e2e',

                'menu-bg': '#ffffff',
                'card-bg': '#ffffff',

                'input-bg': '#f5f2ed',
                'input-border': '#e0ddd8',

                'scrollbar-track': '#f5f2ed',
                'scrollbar-thumb': '#dbd8d3',

                'radius-sm': '6',
                'radius-md': '10',
                'radius-lg': '14'
            }
        }
    };

    /**
     * Theme Manager Class
     */
    class ThemeManager {
        constructor() {
            this.currentTheme = null;
            this.customThemes = {};
            this.styleElement = null;
        }

        /**
         * Initialize the theme manager
         */
        init() {
            this.loadCustomThemes();
            this.createStyleElement();

            // Load saved theme or default
            const savedThemeId = this.getSavedThemeId();
            if (savedThemeId) {
                this.applyTheme(savedThemeId);
            } else {
                this.applyTheme('dark');
            }

            // Listen for theme changes from other pages/tabs via localStorage
            const self = this;
            window.addEventListener('storage', function(e) {
                if (e.key === 'ultralight_active_theme' && e.newValue) {
                    self.applyTheme(e.newValue);
                }
            });

            // Also poll for changes periodically (backup for same-origin frames)
            this._lastThemeId = savedThemeId || 'dark';
            setInterval(function() {
                const currentThemeId = self.getSavedThemeId();
                if (currentThemeId !== self._lastThemeId) {
                    self._lastThemeId = currentThemeId;
                    self.applyTheme(currentThemeId);
                }
            }, 500);
        }

        /**
         * Create the style element for dynamic theme injection
         */
        createStyleElement() {
            if (!this.styleElement) {
                this.styleElement = document.createElement('style');
                this.styleElement.id = 'theme-dynamic-styles';
                document.head.appendChild(this.styleElement);
            }
        }

        /**
         * Get all available themes (built-in + custom)
         */
        getAllThemes() {
            return { ...DEFAULT_THEMES, ...this.customThemes };
        }

        /**
         * Get a specific theme by ID
         */
        getTheme(themeId) {
            return this.getAllThemes()[themeId] || null;
        }

        /**
         * Get the saved theme ID from storage
         */
        getSavedThemeId() {
            try {
                if (window.NativeGetSetting) {
                    return window.NativeGetSetting('theme') || 'dark';
                }
                return localStorage.getItem(THEME_STORAGE_KEY) || 'dark';
            } catch (e) {
                return 'dark';
            }
        }

        /**
         * Save the active theme ID
         */
        saveThemeId(themeId) {
            try {
                if (window.NativeSetSetting) {
                    window.NativeSetSetting('theme', themeId);
                }
                localStorage.setItem(THEME_STORAGE_KEY, themeId);
            } catch (e) {
                console.warn('Failed to save theme:', e);
            }
        }

        /**
         * Load custom themes from storage
         */
        loadCustomThemes() {
            try {
                if (window.NativeGetThemes) {
                    const json = window.NativeGetThemes();
                    this.customThemes = JSON.parse(json || '{}');
                } else {
                    const stored = localStorage.getItem(CUSTOM_THEMES_KEY);
                    this.customThemes = stored ? JSON.parse(stored) : {};
                }
            } catch (e) {
                console.warn('Failed to load custom themes:', e);
                this.customThemes = {};
            }
        }

        /**
         * Save custom themes to storage
         */
        saveCustomThemes() {
            try {
                const json = JSON.stringify(this.customThemes);
                if (window.NativeSaveThemes) {
                    window.NativeSaveThemes(json);
                }
                localStorage.setItem(CUSTOM_THEMES_KEY, json);
            } catch (e) {
                console.warn('Failed to save custom themes:', e);
            }
        }

        /**
         * Apply a theme by ID
         */
        applyTheme(themeId) {
            const theme = this.getTheme(themeId);
            if (!theme) {
                console.warn('Theme not found:', themeId);
                return false;
            }

            this.currentTheme = theme;

            // Complete set of CSS variables with fallback generation
            const colors = theme.colors;
            const completeColors = { ...colors };

            // Generate missing toolbar variables from existing colors
            if (!completeColors['toolbar-border']) {
                completeColors['toolbar-border'] = colors['color-border-secondary'] || '#252532';
            }
            if (!completeColors['toolbar-icon-disabled']) {
                completeColors['toolbar-icon-disabled'] = colors['color-text-muted'] || '#636074';
            }
            if (!completeColors['toolbar-icon-hover-bg']) {
                completeColors['toolbar-icon-hover-bg'] = colors['color-bg-hover'] || '#343446';
            }

            // Generate missing address bar variables
            if (!completeColors['address-bg']) {
                completeColors['address-bg'] = colors['color-bg-primary'] || '#16151d';
            }
            if (!completeColors['address-text']) {
                completeColors['address-text'] = colors['color-text-secondary'] || '#c4c2d0';
            }
            if (!completeColors['address-focus-text']) {
                completeColors['address-focus-text'] = colors['color-text-primary'] || '#ffffff';
            }
            if (!completeColors['address-focus-ring']) {
                completeColors['address-focus-ring'] = colors['color-bg-hover'] || '#343446';
            }

            // Generate missing menu variables
            if (!completeColors['menu-border']) {
                completeColors['menu-border'] = colors['color-border-primary'] || '#313146';
            }
            if (!completeColors['menu-item-hover']) {
                completeColors['menu-item-hover'] = colors['color-bg-hover'] || '#343446';
            }
            if (!completeColors['menu-text']) {
                completeColors['menu-text'] = colors['color-text-primary'] || '#e2e1ea';
            }
            if (!completeColors['menu-separator']) {
                completeColors['menu-separator'] = colors['color-border-primary'] || '#3a3a4e';
            }

            // Generate missing tooltip variables
            if (!completeColors['tooltip-bg']) {
                completeColors['tooltip-bg'] = colors['color-bg-overlay'] || 'rgba(43, 43, 56, 0.95)';
            }
            if (!completeColors['tooltip-text']) {
                completeColors['tooltip-text'] = colors['color-text-primary'] || '#f0f0ff';
            }

            // Generate missing button variables
            if (!completeColors['btn-primary-bg']) {
                completeColors['btn-primary-bg'] = colors['color-accent-primary'] || '#6C63FF';
            }
            if (!completeColors['btn-primary-hover']) {
                completeColors['btn-primary-hover'] = colors['color-accent-hover'] || '#7a72ff';
            }
            if (!completeColors['btn-secondary-bg']) {
                completeColors['btn-secondary-bg'] = 'rgba(255, 255, 255, 0.1)';
            }
            if (!completeColors['btn-secondary-hover']) {
                completeColors['btn-secondary-hover'] = 'rgba(255, 255, 255, 0.15)';
            }

            // Ensure a color-accent-light exists for glows and shadows (derived from accent color)
            if (!completeColors['color-accent-light']) {
                const accentHex = colors['color-accent-primary'] || colors['color-accent'] || completeColors['btn-primary-bg'];
                function hexToRgb(hex) {
                    if (!hex) return null;
                    const clean = hex.replace('#','');
                    if (clean.length === 3) {
                        const r = parseInt(clean[0] + clean[0], 16);
                        const g = parseInt(clean[1] + clean[1], 16);
                        const b = parseInt(clean[2] + clean[2], 16);
                        return {r,g,b};
                    }
                    if (clean.length === 6) {
                        const r = parseInt(clean.substring(0,2), 16);
                        const g = parseInt(clean.substring(2,4), 16);
                        const b = parseInt(clean.substring(4,6), 16);
                        return {r,g,b};
                    }
                    return null;
                }
                let rgbaVal = 'rgba(108, 99, 255, 0.15)';
                if (/^#/.test(accentHex)) {
                    const rgb = hexToRgb(accentHex);
                    if (rgb) rgbaVal = `rgba(${rgb.r}, ${rgb.g}, ${rgb.b}, 0.15)`;
                }
                completeColors['color-accent-light'] = rgbaVal;
            }
            // Alias for older pages that use --accent-light
            if (!completeColors['accent-light']) {
                completeColors['accent-light'] = completeColors['color-accent-light'];
            }

            // Provide short, consistent aliases so pages can use simple variables
            // (avoids having to search & replace many files; pages can use --accent-color)
            if (!completeColors['accent-color']) {
                completeColors['accent-color'] = completeColors['color-accent-primary'] || completeColors['btn-primary-bg'] || '#6C63FF';
            }
            if (!completeColors['accent-hover']) {
                completeColors['accent-hover'] = completeColors['color-accent-hover'] || completeColors['btn-primary-hover'] || completeColors['color-accent-secondary'] || '#8a83ff';
            }
            // Legacy alias
            if (!completeColors['accent']) {
                completeColors['accent'] = completeColors['accent-color'];
            }
            // Short aliases for buttons
            if (!completeColors['btn-bg']) {
                completeColors['btn-bg'] = completeColors['btn-primary-bg'] || completeColors['accent-color'];
            }
            if (!completeColors['btn-hover-bg']) {
                completeColors['btn-hover-bg'] = completeColors['btn-primary-hover'] || completeColors['accent-hover'];
            }

            // Shadow aliases derived from accent color for stronger visual parity with Themes page
            if (!completeColors['accent-shadow']) {
                const accentHex2 = colors['color-accent-primary'] || colors['color-accent'] || completeColors['accent-color'];
                function hexToRgbLocal(hex) {
                    if (!hex) return null;
                    const clean = hex.replace('#','');
                    if (clean.length === 3) {
                        return {
                            r: parseInt(clean[0] + clean[0], 16),
                            g: parseInt(clean[1] + clean[1], 16),
                            b: parseInt(clean[2] + clean[2], 16)
                        };
                    }
                    if (clean.length === 6) {
                        return {
                            r: parseInt(clean.substring(0,2), 16),
                            g: parseInt(clean.substring(2,4), 16),
                            b: parseInt(clean.substring(4,6), 16)
                        };
                    }
                    return null;
                }
                const rgb2 = hexToRgbLocal(accentHex2);
                if (rgb2) {
                    completeColors['accent-shadow'] = `rgba(${rgb2.r}, ${rgb2.g}, ${rgb2.b}, 0.30)`;
                    completeColors['accent-shadow-strong'] = `rgba(${rgb2.r}, ${rgb2.g}, ${rgb2.b}, 0.40)`;
                    completeColors['accent-shadow-subtle'] = `rgba(${rgb2.r}, ${rgb2.g}, ${rgb2.b}, 0.12)`;
                } else {
                    completeColors['accent-shadow'] = 'rgba(108, 99, 255, 0.30)';
                    completeColors['accent-shadow-strong'] = 'rgba(108, 99, 255, 0.40)';
                    completeColors['accent-shadow-subtle'] = 'rgba(108, 99, 255, 0.12)';
                }
            }

            // Generate missing session restore bar variables
            if (!completeColors['session-restore-bg']) {
                const bgActive = colors['color-bg-active'] || '#3d3d5c';
                const bgTertiary = colors['color-bg-tertiary'] || '#2d2d44';
                completeColors['session-restore-bg'] = `linear-gradient(180deg, ${bgActive} 0%, ${bgTertiary} 100%)`;
            }
            if (!completeColors['session-restore-border']) {
                completeColors['session-restore-border'] = colors['color-border-primary'] || '#4a4a6a';
            }
            if (!completeColors['session-restore-text']) {
                completeColors['session-restore-text'] = colors['color-text-primary'] || '#e4e4ef';
            }
            if (!completeColors['session-restore-icon']) {
                completeColors['session-restore-icon'] = colors['color-text-tertiary'] || '#9090b0';
            }

            // Generate missing DRM bar variables
            if (!completeColors['drm-bar-bg']) {
                const accentPrimary = colors['color-accent-primary'] || '#6C63FF';
                const accentSecondary = colors['color-accent-secondary'] || '#7c6aef';
                completeColors['drm-bar-bg'] = `linear-gradient(135deg, ${accentPrimary}, ${accentSecondary})`;
            }
            if (!completeColors['drm-bar-text']) {
                completeColors['drm-bar-text'] = '#ffffff';
            }
            if (!completeColors['drm-btn-bg']) {
                completeColors['drm-btn-bg'] = 'rgba(255, 255, 255, 0.15)';
            }
            if (!completeColors['drm-btn-hover']) {
                completeColors['drm-btn-hover'] = 'rgba(255, 255, 255, 0.25)';
            }
            if (!completeColors['drm-btn-primary-bg']) {
                completeColors['drm-btn-primary-bg'] = '#ffffff';
            }
            if (!completeColors['drm-btn-primary-text']) {
                completeColors['drm-btn-primary-text'] = colors['color-accent-primary'] || '#6C63FF';
            }

            // Generate tab gradient backgrounds (matching tabs and add-tab button)
            const bgSecondary = colors['color-bg-secondary'] || '#1c1c24';
            const bgTertiary = colors['color-bg-tertiary'] || '#232330';
            const bgElevated = colors['color-bg-elevated'] || '#282839';
            const bgHover = colors['color-bg-hover'] || '#343446';

            if (!completeColors['tab-bg']) {
                completeColors['tab-bg'] = `linear-gradient(180deg, ${bgTertiary}, ${bgSecondary})`;
            }
            if (!completeColors['tab-active-bg']) {
                completeColors['tab-active-bg'] = `linear-gradient(180deg, ${bgElevated}, ${bgTertiary})`;
            }
            if (!completeColors['tab-hover-bg']) {
                completeColors['tab-hover-bg'] = `linear-gradient(180deg, ${bgHover}, ${bgElevated})`;
            }
            if (!completeColors['add-tab-border']) {
                completeColors['add-tab-border'] = colors['toolbar-border'] || colors['color-border-secondary'] || '#252532';
            }
            if (!completeColors['add-tab-text']) {
                completeColors['add-tab-text'] = colors['color-text-secondary'] || '#c4c2d0';
            }

            // Generate CSS from complete colors
            let css = ':root {\n';
            for (const [key, value] of Object.entries(completeColors)) {
                css += `    --${key}: ${value};\n`;
            }
            css += '}\n';

            // Apply to style element
            if (this.styleElement) {
                this.styleElement.textContent = css;
            }

            // Save preference
            this.saveThemeId(themeId);

            // Dispatch event for other components
            window.dispatchEvent(new CustomEvent('themeChanged', {
                detail: { themeId, theme }
            }));

            return true;
        }

        /**
         * Create a new custom theme
         */
        createTheme(themeData) {
            const id = themeData.id || 'custom_' + Date.now();

            const theme = {
                id: id,
                name: themeData.name || 'Custom Theme',
                description: themeData.description || '',
                author: themeData.author || 'User',
                version: themeData.version || '1.0.0',
                category: themeData.category || 'dark',
                isBuiltIn: false,
                colors: themeData.colors || { ...DEFAULT_THEMES.dark.colors }
            };

            this.customThemes[id] = theme;
            this.saveCustomThemes();

            return theme;
        }

        /**
         * Update an existing custom theme
         */
        updateTheme(themeId, updates) {
            if (DEFAULT_THEMES[themeId]) {
                console.warn('Cannot modify built-in theme');
                return false;
            }

            if (!this.customThemes[themeId]) {
                console.warn('Custom theme not found:', themeId);
                return false;
            }

            this.customThemes[themeId] = {
                ...this.customThemes[themeId],
                ...updates,
                id: themeId // Ensure ID doesn't change
            };

            this.saveCustomThemes();

            // Re-apply if this is the current theme
            if (this.currentTheme && this.currentTheme.id === themeId) {
                this.applyTheme(themeId);
            }

            return true;
        }

        /**
         * Delete a custom theme
         */
        deleteTheme(themeId) {
            if (DEFAULT_THEMES[themeId]) {
                console.warn('Cannot delete built-in theme');
                return false;
            }

            if (!this.customThemes[themeId]) {
                return false;
            }

            delete this.customThemes[themeId];
            this.saveCustomThemes();

            // Switch to default if deleting current theme
            if (this.currentTheme && this.currentTheme.id === themeId) {
                this.applyTheme('dark');
            }

            return true;
        }

        /**
         * Export a theme as JSON
         */
        exportTheme(themeId) {
            const theme = this.getTheme(themeId);
            if (!theme) return null;

            return JSON.stringify(theme, null, 2);
        }

        /**
         * Import a theme from JSON
         */
        importTheme(jsonString) {
            try {
                const theme = JSON.parse(jsonString);

                if (!theme.name || !theme.colors) {
                    throw new Error('Invalid theme format');
                }

                // Generate new ID for imported theme
                theme.id = 'imported_' + Date.now();
                theme.isBuiltIn = false;

                return this.createTheme(theme);
            } catch (e) {
                console.error('Failed to import theme:', e);
                return null;
            }
        }

        /**
         * Get the current theme
         */
        getCurrentTheme() {
            return this.currentTheme;
        }

        /**
         * Duplicate a theme for customization
         */
        duplicateTheme(themeId) {
            const source = this.getTheme(themeId);
            if (!source) return null;

            return this.createTheme({
                name: source.name + ' (Copy)',
                description: source.description,
                author: 'User',
                colors: { ...source.colors }
            });
        }
    }

    // Create global instance
    window.ThemeManager = new ThemeManager();

    // Auto-initialize when DOM is ready
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', () => {
            window.ThemeManager.init();
        });
    } else {
        window.ThemeManager.init();
    }

})();
