(() => {
  const buttons = [...document.querySelectorAll('.filter')];
  const cards = [...document.querySelectorAll('.profile')];
  const count = document.querySelector('#profile-count');

  if (!buttons.length || !cards.length || !count)
    return;

  const applyFilter = (filter) => {
    let visible = 0;
    for (const card of cards) {
      const show = filter === 'all' || card.dataset.platform === filter;
      card.hidden = !show;
      if (show)
        visible += 1;
    }

    count.textContent = `${visible} profile${visible === 1 ? '' : 's'}`;
    for (const button of buttons)
      button.classList.toggle('active', button.dataset.filter === filter);
  };

  for (const button of buttons) {
    button.addEventListener('click', () => applyFilter(button.dataset.filter));
  }
})();
