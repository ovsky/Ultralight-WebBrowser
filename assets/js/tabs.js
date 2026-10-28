(function () {
  const isNodeContext = typeof module !== 'undefined' && typeof module.exports !== 'undefined'
  if (isNodeContext) {
    Draggabilly = require('draggabilly')
  }

  const tabTemplate = `
    <div class="tab">
      <div class="tab-background">
      </div>
      <div class="tab-favicon"></div>
      <div class="tab-spinner"></div>
      <div class="tab-title">
        <span class="tab-title-text"></span>
        <span class="tab-badge" aria-hidden="true"></span>
      </div>
      <div class="tab-close"></div>
    </div>
  `

  const defaultTabProperties = {
    title: '',
    favicon: ''
  }

  let instanceId = 0
  let tabId = 0

  class Tabs {
    constructor() {
      this.draggabillyInstances = []
      this.addButtonGap = 8
    }

    init(el, options) {
      this.el = el
      this.options = options
      this.addButtonEl = this.el.querySelector('#tabs-add-tab')
      const contentEl = this.tabContentEl
      if (this.addButtonEl && contentEl && this.addButtonEl.parentNode !== contentEl) {
        contentEl.appendChild(this.addButtonEl)
      }
      if (typeof this.options.addButtonGap === 'number') {
        this.addButtonGap = this.options.addButtonGap
      }

      this.instanceId = instanceId
      this.el.setAttribute('data-tabs-instance-id', this.instanceId)
      instanceId += 1

      this.tabId = tabId

      this.setupStyleEl()
      this.setupEvents()
      this.layoutTabs()
      this.fixZIndexes()
      this.setupDraggabilly()
    }

    emit(eventName, data) {
      this.el.dispatchEvent(new CustomEvent(eventName, { detail: data }))
    }

    setupStyleEl() {
      this.animationStyleEl = document.createElement('style')
      this.el.appendChild(this.animationStyleEl)
    }

    setupEvents() {
      window.addEventListener('resize', event => this.layoutTabs())

      if (this.addButtonEl) {
        this.addButtonEl.addEventListener('click', event => this.emit('requestNewTab'))
      }

      this.el.addEventListener('click', ({ target }) => {
        if (target.classList.contains('tab')) {
          this.setCurrentTab(target)
        } else if (target.classList.contains('tab-close')) {
          this.emit('requestTabClose', { tabEl: target.parentNode })
        } else if (target.classList.contains('tab-title') || target.classList.contains('tab-favicon')) {
          this.setCurrentTab(target.parentNode)
        }
      })
    }

    get tabEls() {
      return Array.prototype.slice.call(this.el.querySelectorAll('.tab'))
    }

    get tabContentEl() {
      return this.el.querySelector('.tabs-content')
    }

    get tabWidth() {
      const tabCount = this.tabEls.length
      if (!tabCount) {
        return this.options.maxWidth
      }

      const availableWidth = this.tabContentEl.clientWidth
      const addButtonWidth = this.getAddButtonWidth()
      const gap = tabCount ? this.addButtonGap : 0
      const totalAtMax = this.calculateTabsTotalWidth(this.options.maxWidth, tabCount)

      if (totalAtMax + addButtonWidth + gap <= availableWidth) {
        return this.options.maxWidth
      }

      const availableForTabs = Math.max(0, availableWidth - addButtonWidth - gap)
      const width = ((availableForTabs - this.options.tabOverlapDistance) / tabCount) + this.options.tabOverlapDistance
      return Math.max(this.options.minWidth, Math.min(this.options.maxWidth, width))
    }

    get tabEffectiveWidth() {
      return this.tabWidth - this.options.tabOverlapDistance
    }

    get tabPositions() {
      const tabEffectiveWidth = this.tabEffectiveWidth
      let left = 0
      let positions = []

      this.tabEls.forEach((tabEl, i) => {
        positions.push(left)
        left += tabEffectiveWidth
      })
      return positions
    }

    layoutTabs() {
      const tabWidth = this.tabWidth

      this.cleanUpPreviouslyDraggedTabs()
      this.tabEls.forEach((tabEl) => tabEl.style.width = tabWidth + 'px')
      const tabPositions = this.tabPositions
      requestAnimationFrame(() => {
        let styleHTML = ''
        tabPositions.forEach((left, i) => {
          styleHTML += `
            .tabs[data-tabs-instance-id="${this.instanceId}"] .tab:nth-child(${i + 1}) {
              transform: translate3d(${left}px, 0, 0)
            }
          `
        })
        this.animationStyleEl.innerHTML = styleHTML
        this.positionAddButton(tabPositions, tabWidth)
      })
      if (!this.tabEls.length) {
        this.positionAddButton([], tabWidth)
      }
    }

    positionAddButton(tabPositions, tabWidth) {
      if (!this.addButtonEl) return
      const buttonWidth = this.getAddButtonWidth()
      const contentEl = this.tabContentEl
      if (!contentEl) return
      const maxInsideLeft = Math.max(0, contentEl.clientWidth - buttonWidth)
      let insideLeft = 0

      if (tabPositions.length) {
        const lastLeft = tabPositions[tabPositions.length - 1]
        const lastRight = lastLeft + tabWidth - this.options.tabOverlapDistance
        insideLeft = Math.min(lastRight + this.addButtonGap, maxInsideLeft)
      } else {
        insideLeft = Math.min(this.addButtonGap, maxInsideLeft)
      }

      this.addButtonEl.style.left = `${Math.round(insideLeft)}px`
      this.addButtonEl.style.right = 'auto'
      this.addButtonEl.style.transform = ''
    }

    getAddButtonWidth() {
      if (!this.addButtonEl) return 0
      const rect = this.addButtonEl.getBoundingClientRect()
      if (rect.width) return rect.width
      return this.addButtonEl.offsetWidth || 0
    }

    calculateTabsTotalWidth(tabWidth, tabCount) {
      if (!tabCount) return 0
      const overlap = this.options.tabOverlapDistance
      return tabWidth + (tabCount - 1) * (tabWidth - overlap)
    }

    fixZIndexes() {
      const bottomBarEl = this.el.querySelector('.tabs-bottom-bar')
      const tabEls = this.tabEls

      tabEls.forEach((tabEl, i) => {
        let zIndex = tabEls.length - i

        if (tabEl.classList.contains('tab-current')) {
          bottomBarEl.style.zIndex = tabEls.length + 1
          zIndex = tabEls.length + 2
        }
        tabEl.style.zIndex = zIndex
      })
    }

    createNewTabEl() {
      const div = document.createElement('div')
      div.innerHTML = tabTemplate

      return div.firstElementChild
    }

    addTab(tabProperties) {
      const tabEl = this.createNewTabEl()
      tabEl.setAttribute('data-tab-id', tabProperties.id)

      tabEl.classList.add('tab-just-added')
      setTimeout(() => tabEl.classList.remove('tab-just-added'), 500)

      tabProperties = Object.assign({}, defaultTabProperties, tabProperties)
      const contentEl = this.tabContentEl
      if (this.addButtonEl && contentEl && this.addButtonEl.parentNode === contentEl) {
        contentEl.insertBefore(tabEl, this.addButtonEl)
      } else {
        contentEl.appendChild(tabEl)
      }
      this.updateTab(tabEl, tabProperties)

      const tabWidth = this.tabWidth
      const tabEffectiveWidth = tabWidth - this.options.tabOverlapDistance
      tabEl.style.width = `${tabWidth}px`
      const lastIndex = this.tabEls.length - 1
      const initialLeft = Math.max(0, lastIndex * tabEffectiveWidth)
      tabEl.style.transform = `translate3d(${initialLeft}px, 0, 0)`

      this.emit('tabAdd', { tabEl })
      this.setCurrentTab(tabEl)
      this.layoutTabs()
      this.fixZIndexes()
      this.setupDraggabilly()
    }

    setCurrentTab(tabEl) {
      const currentTab = this.el.querySelector('.tab-current')
      if (currentTab) currentTab.classList.remove('tab-current')
      tabEl.classList.add('tab-current')
      this.fixZIndexes()
      this.emit('activeTabChange', { tabEl })
    }

    removeTab(tabEl) {
      if (tabEl.classList.contains('tab-current')) {
        if (tabEl.previousElementSibling) {
          this.setCurrentTab(tabEl.previousElementSibling)
        } else if (tabEl.nextElementSibling) {
          this.setCurrentTab(tabEl.nextElementSibling)
        }
      }
      tabEl.parentNode.removeChild(tabEl)
      this.emit('tabRemove', { tabEl })
      this.layoutTabs()
      this.fixZIndexes()
      this.setupDraggabilly()
    }

    updateTab(tabEl, tabProperties) {
      const titleEl = tabEl.querySelector('.tab-title-text') || tabEl.querySelector('.tab-title')
      if (titleEl) titleEl.textContent = tabProperties.title
      tabEl.querySelector('.tab-favicon').style.backgroundImage = `url(${tabProperties.favicon})`
      tabEl.querySelector('.tab-favicon').style.display = tabProperties.loading ? 'none' : 'inline-block'
      tabEl.querySelector('.tab-spinner').style.display = tabProperties.loading ? 'inline-block' : 'none'
    }

    cleanUpPreviouslyDraggedTabs() {
      this.tabEls.forEach((tabEl) => tabEl.classList.remove('tab-just-dragged'))
    }

    setupDraggabilly() {
      const tabEls = this.tabEls
      const tabEffectiveWidth = this.tabEffectiveWidth
      const tabPositions = this.tabPositions

      this.draggabillyInstances.forEach(draggabillyInstance => draggabillyInstance.destroy())

      tabEls.forEach((tabEl, originalIndex) => {
        const originalTabPositionX = tabPositions[originalIndex]
        const draggabillyInstance = new Draggabilly(tabEl, {
          axis: 'x',
          containment: this.tabContentEl
        })

        this.draggabillyInstances.push(draggabillyInstance)

        draggabillyInstance.on('dragStart', () => {
          this.cleanUpPreviouslyDraggedTabs()
          tabEl.classList.add('tab-currently-dragged')
          this.el.classList.add('tabs-sorting')
          this.fixZIndexes()
        })

        draggabillyInstance.on('dragEnd', () => {
          const finalTranslateX = parseFloat(tabEl.style.left, 10)
          tabEl.style.transform = `translate3d(0, 0, 0)`

          // Animate dragged tab back into its place
          requestAnimationFrame(() => {
            tabEl.style.left = '0'
            tabEl.style.transform = `translate3d(${finalTranslateX}px, 0, 0)`

            requestAnimationFrame(() => {
              tabEl.classList.remove('tab-currently-dragged')
              this.el.classList.remove('tabs-sorting')

              this.setCurrentTab(tabEl)
              tabEl.classList.add('tab-just-dragged')

              requestAnimationFrame(() => {
                tabEl.style.transform = ''

                this.setupDraggabilly()
              })
            })
          })
        })

        draggabillyInstance.on('dragMove', (event, pointer, moveVector) => {
          // Current index be computed within the event since it can change during the dragMove
          const tabEls = this.tabEls
          const currentIndex = tabEls.indexOf(tabEl)

          const currentTabPositionX = originalTabPositionX + moveVector.x
          const destinationIndex = Math.max(0, Math.min(tabEls.length, Math.floor((currentTabPositionX + (tabEffectiveWidth / 2)) / tabEffectiveWidth)))

          if (currentIndex !== destinationIndex) {
            this.animateTabMove(tabEl, currentIndex, destinationIndex)
          }
        })
      })
    }

    animateTabMove(tabEl, originIndex, destinationIndex) {
      if (destinationIndex < originIndex) {
        tabEl.parentNode.insertBefore(tabEl, this.tabEls[destinationIndex])
      } else {
        tabEl.parentNode.insertBefore(tabEl, this.tabEls[destinationIndex + 1])
      }
    }
  }

  if (isNodeContext) {
    module.exports = ChromeTabs
  } else {
    window.ChromeTabs = ChromeTabs
  }
})()
