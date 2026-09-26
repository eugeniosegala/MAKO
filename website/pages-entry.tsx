import { StrictMode } from 'react';
import { hydrateRoot } from 'react-dom/client';
import Home from './app/page';
import './app/globals.css';

hydrateRoot(
  document.getElementById('root')!,
  <StrictMode>
    <Home />
  </StrictMode>,
);
