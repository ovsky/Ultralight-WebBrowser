window.__ul_settings = window.__ul_settings || {};

function updateBack(enable) {
	if (enable)
		document.getElementById("back").classList.remove("disabled");
	else
		document.getElementById("back").classList.add("disabled");
}

function updateForward(enable) {
	if (enable)
		document.getElementById("forward").classList.remove("disabled");
	else
		document.getElementById("forward").classList.add("disabled");
}

function updateLoading(is_loading) {
	if (is_loading) {
		document.getElementById("refresh").style.display = "none";
		document.getElementById("stop").style.display = "inline-block";
	} else {
		document.getElementById("refresh").style.display = "inline-block";
		document.getElementById("stop").style.display = "none";
	}
}

function updateURL(url) {
	document.getElementById('address').value = url;
}

function setTabDrmState(tabId, isDrm) {
	const tab = document.querySelector(".chrome-tab[data-tab-id='" + tabId + "']");
	if (!tab) return;
	const isBadgeVisible = !!isDrm;
	tab.classList.toggle('is-drm-tab', isBadgeVisible);
	const badge = tab.querySelector('.chrome-tab-badge');
	if (!badge) return;
	if (isBadgeVisible) {
		badge.textContent = '[DRM]';
		badge.title = 'This tab is using the secure DRM WebView';
	} else {
		badge.textContent = '';
		badge.removeAttribute('title');
	}
}

function focusAddressBar() {
	let address = document.getElementById('address');
	address.focus();
	address.select();
}

document.getElementById('address').addEventListener('blur', () => {
	if (window.OnAddressBarBlur) {
		window.OnAddressBarBlur();
	}
});

// Notify native when the address bar gains focus (eg, via mouse click)
document.getElementById('address').addEventListener('focus', () => {
	if (window.OnAddressBarFocus) {
		window.OnAddressBarFocus();
	}
});

// Update AdBlock toggle visual state: when enabled, normal; when disabled, grey out
function updateAdblockEnabled(enabled) {
	const el = document.getElementById('toggle-adblock');
	if (!el) return;
	const isEnabled = !!enabled;
	el.classList.remove('inactive');
	el.classList.toggle('active', isEnabled);
	el.setAttribute('aria-pressed', isEnabled ? 'true' : 'false');
	el.dataset.state = isEnabled ? 'on' : 'off';
}

function applySettings(payload) {
	let parsed = null;
	try {
		parsed = (typeof payload === 'string') ? JSON.parse(payload || '{}') : payload;
	} catch (e) {
		return;
	}
	if (!parsed || typeof parsed !== 'object') return;
	window.__ul_settings = parsed;
	const applyToBody = () => {
		const body = document.body;
		if (!body) return;
		body.classList.toggle('transparent-toolbar', !!parsed.experimental_transparent_toolbar);
		body.classList.toggle('compact-tabs', !!parsed.experimental_compact_tabs);
	};
	if (document.readyState === 'loading' && !document.body) {
		document.addEventListener('DOMContentLoaded', applyToBody, { once: true });
	} else {
		applyToBody();
	}
	if (typeof window.__ul_update_downloads_badge === 'function') {
		window.__ul_update_downloads_badge();
	}
if (parsed.enable_suggestions === false && typeof CloseSuggestionsOverlay === 'function') {
 		CloseSuggestionsOverlay();
 	}
 }

// ============================================================================
// Tab Groups Manager
// ============================================================================

class TabGroupsManager {
    constructor(chromeTabs) {
        this.chromeTabs = chromeTabs;
        this.groups = new Map(); // group_id -> { id, title, color, collapsed, tab_ids: [] }
        this.tabToGroup = new Map(); // tab_id -> group_id
        this.groupElements = new Map(); // group_id -> DOM element
        this.nextGroupId = 1;
        
        this.init();
    }
    
    init() {
        // Listen for tab group events from native
        window.addEventListener('tabGroupCreated', (e) => this.onTabGroupCreated(e.detail));
        window.addEventListener('tabGroupDeleted', (e) => this.onTabGroupDeleted(e.detail));
        window.addEventListener('tabGroupUpdated', (e) => this.onTabGroupUpdated(e.detail));
        window.addEventListener('tabGroupChanged', (e) => this.onTabGroupChanged(e.detail));
        
        // Load initial groups from native
        this.loadGroups();
    }
    
    async loadGroups() {
        if (typeof GetTabGroups === 'function') {
            try {
                const json = await GetTabGroups();
                const groups = JSON.parse(json);
                this.groups.clear();
                this.tabToGroup.clear();
                
                for (const group of groups) {
                    this.groups.set(group.id, group);
                    for (const tabId of group.tab_ids) {
                        this.tabToGroup.set(tabId, group.id);
                    }
                    this.renderGroup(group);
                }
                
                // Mark tabs as in-group
                this.updateTabGroupIndicators();
            } catch (e) {
                console.error('Failed to load tab groups:', e);
            }
        }
    }
    
    renderGroup(group) {
        const contentEl = this.chromeTabs.tabContentEl;
        if (!contentEl) return;
        
        // Check if group element already exists
        let groupEl = this.groupElements.get(group.id);
        if (groupEl) {
            this.updateGroupElement(groupEl, group);
            return;
        }
        
        // Create group header element
        groupEl = document.createElement('div');
        groupEl.className = 'chrome-tab-group' + (group.collapsed ? ' collapsed' : '');
        groupEl.dataset.groupId = group.id;
        
        const chevronSvg = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="6 9 12 15 18 9"></polyline></svg>';
        
        groupEl.innerHTML = `
            <div class="chrome-tab-group-header" data-group-id="${group.id}">
                <div class="chrome-tab-group-color" style="background: ${group.color};"></div>
                <span class="chrome-tab-group-title">${this.escapeHtml(group.title)}</span>
                <button class="chrome-tab-group-toggle" data-group-id="${group.id}" aria-label="Toggle group">
                    ${chevronSvg}
                </button>
            </div>
        `;
        
        // Add click handler for toggle
        const toggleBtn = groupEl.querySelector('.chrome-tab-group-toggle');
        if (toggleBtn) {
            toggleBtn.addEventListener('click', (e) => {
                e.stopPropagation();
                this.toggleGroup(group.id);
            });
        }
        
        // Add click handler for header (could be used for rename in future)
        const header = groupEl.querySelector('.chrome-tab-group-header');
        if (header) {
            header.addEventListener('click', (e) => {
                if (e.target === header || e.target.classList.contains('chrome-tab-group-title') || e.target.classList.contains('chrome-tab-group-color')) {
                    // Could open rename dialog here
                }
            });
        }
        
        // Insert before the add tab button
        if (this.chromeTabs.addButtonEl && this.chromeTabs.addButtonEl.parentNode === contentEl) {
            contentEl.insertBefore(groupEl, this.chromeTabs.addButtonEl);
        } else {
            contentEl.appendChild(groupEl);
        }
        
        this.groupElements.set(group.id, groupEl);
        this.updateGroupPosition(group.id);
    }
    
    updateGroupElement(groupEl, group) {
        groupEl.classList.toggle('collapsed', group.collapsed);
        const colorEl = groupEl.querySelector('.chrome-tab-group-color');
        const titleEl = groupEl.querySelector('.chrome-tab-group-title');
        if (colorEl) colorEl.style.background = group.color;
        if (titleEl) titleEl.textContent = group.title;
        this.updateGroupPosition(group.id);
    }
    
    updateGroupPosition(groupId) {
        const groupEl = this.groupElements.get(groupId);
        const group = this.groups.get(groupId);
        if (!groupEl || !group) return;
        
        // Position the group header at the position of the first tab in the group
        if (group.tab_ids.length > 0) {
            const firstTabId = group.tab_ids[0];
            const firstTabEl = document.querySelector("[data-tab-id='" + firstTabId + "']");
            if (firstTabEl) {
                const rect = firstTabEl.getBoundingClientRect();
                const contentRect = this.chromeTabs.tabContentEl.getBoundingClientRect();
                groupEl.style.transform = `translate3d(${rect.left - contentRect.left}px, 0, 0)`;
            }
        }
    }
    
    updateAllGroupPositions() {
        for (const groupId of this.groupElements.keys()) {
            this.updateGroupPosition(groupId);
        }
    }
    
    onTabGroupCreated(detail) {
        const groupId = detail;
        if (typeof GetTabGroups === 'function') {
            GetTabGroups().then(json => {
                const groups = JSON.parse(json);
                const group = groups.find(g => g.id === groupId);
                if (group) {
                    this.groups.set(group.id, group);
                    for (const tabId of group.tab_ids) {
                        this.tabToGroup.set(tabId, group.id);
                    }
                    this.renderGroup(group);
                    this.updateTabGroupIndicators();
                }
            });
        }
    }
    
    onTabGroupDeleted(detail) {
        const groupId = detail;
        const groupEl = this.groupElements.get(groupId);
        if (groupEl) {
            groupEl.remove();
            this.groupElements.delete(groupId);
        }
        const group = this.groups.get(groupId);
        if (group) {
            for (const tabId of group.tab_ids) {
                this.tabToGroup.delete(tabId);
            }
            this.groups.delete(groupId);
        }
        this.updateTabGroupIndicators();
    }
    
    onTabGroupUpdated(detail) {
        const groupId = detail;
        if (typeof GetTabGroups === 'function') {
            GetTabGroups().then(json => {
                const groups = JSON.parse(json);
                const group = groups.find(g => g.id === groupId);
                if (group) {
                    this.groups.set(group.id, group);
                    // Update tab-to-group mapping
                    for (const tabId of group.tab_ids) {
                        this.tabToGroup.set(tabId, group.id);
                    }
                    this.updateGroupElement(this.groupElements.get(groupId), group);
                    this.updateTabGroupIndicators();
                }
            });
        }
    }
    
    onTabGroupChanged(detail) {
        // Refresh all groups
        this.loadGroups();
    }
    
    updateTabGroupIndicators() {
        // Mark tabs as in-group
        const allTabs = document.querySelectorAll('.chrome-tab');
        allTabs.forEach(tabEl => {
            const tabId = parseInt(tabEl.dataset.tabId, 10);
            const groupId = this.tabToGroup.get(tabId);
            if (groupId) {
                tabEl.classList.add('is-in-group');
                tabEl.dataset.groupId = groupId;
                const group = this.groups.get(groupId);
                if (group) {
                    tabEl.style.setProperty('--tab-group-color', group.color);
                }
            } else {
                tabEl.classList.remove('is-in-group');
                tabEl.removeAttribute('data-group-id');
                tabEl.style.removeProperty('--tab-group-color');
            }
            
            // Hide tabs in collapsed groups
            if (groupId) {
                const group = this.groups.get(groupId);
                if (group && group.collapsed) {
                    // Check if this tab is NOT the first tab in the group
                    const firstTabId = group.tab_ids[0];
                    if (tabId !== firstTabId) {
                        tabEl.classList.add('in-group-hidden');
                    } else {
                        tabEl.classList.remove('in-group-hidden');
                    }
                } else {
                    tabEl.classList.remove('in-group-hidden');
                }
            } else {
                tabEl.classList.remove('in-group-hidden');
            }
        });
    }
    
    toggleGroup(groupId) {
        const group = this.groups.get(groupId);
        if (!group) return;
        
        group.collapsed = !group.collapsed;
        this.groups.set(groupId, group);
        
        const groupEl = this.groupElements.get(groupId);
        if (groupEl) {
            groupEl.classList.toggle('collapsed', group.collapsed);
        }
        
        // Notify native
        if (typeof OnToggleTabGroupCollapsed === 'function') {
            OnToggleTabGroupCollapsed({ groupId });
        }
        
        this.updateTabGroupIndicators();
        this.chromeTabs.layoutTabs();
    }
    
    createGroup(title, color) {
        if (typeof OnCreateTabGroup === 'function') {
            OnCreateTabGroup({ title: title || 'New Group', color: color || '#6C63FF' });
        }
    }
    
    deleteGroup(groupId) {
        if (typeof OnDeleteTabGroup === 'function') {
            OnDeleteTabGroup({ groupId });
        }
    }
    
    addTabToGroup(tabId, groupId) {
        if (typeof OnAddTabToGroup === 'function') {
            OnAddTabToGroup({ tabId, groupId });
        }
    }
    
    removeTabFromGroup(tabId) {
        if (typeof OnRemoveTabFromGroup === 'function') {
            OnRemoveTabFromGroup({ tabId });
        }
    }
    
    moveTabInGroup(tabId, groupId, newIndex) {
        if (typeof OnMoveTabInGroup === 'function') {
            OnMoveTabInGroup({ tabId, groupId, newIndex });
        }
    }
    
    escapeHtml(text) {
        const div = document.createElement('div');
        div.textContent = text;
        return div.innerHTML;
    }
}

// Initialize tab groups manager after chromeTabs is ready
let tabGroupsManager = null;

// Hook into chromeTabs layout to update group positions
const originalLayoutTabs = chromeTabs.layoutTabs.bind(chromeTabs);
chromeTabs.layoutTabs = function() {
    originalLayoutTabs();
    if (tabGroupsManager) {
        tabGroupsManager.updateAllGroupPositions();
    }
};

// Initialize after chromeTabs is created
document.addEventListener('DOMContentLoaded', () => {
    // Give chromeTabs time to initialize
    setTimeout(() => {
        if (window.chromeTabs) {
            tabGroupsManager = new TabGroupsManager(window.chromeTabs);
        }
    }, 100);
});