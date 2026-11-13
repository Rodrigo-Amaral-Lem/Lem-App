# Interface — 0.1.12

- Android: câmara, galeria e **Obs.** na mesma linha, com alvos de toque de 48 px. Os ícones geométricos não dependem de caracteres disponíveis nas fontes. Os nomes completos mantêm-se para acessibilidade e dicas no desktop. No desktop, o ícone de galeria abre a seleção de ficheiros.
- **Obs.** abre as observações opcionais. Voltar a tocar fecha um atributo vazio. Havendo texto, pede confirmação antes de o apagar; cancelar conserva o texto. A resposta e as fotografias permanecem. Em NOK, a observação continua obrigatória e o botão de remoção fica indisponível.
- Faixa permanente de conta: verde com **Sessão iniciada · nome** ou âmbar com **Sem sessão iniciada · Entrar**. Tocar abre a conta. O modo demonstração tem indicação própria. A indicação usa o token da sessão, não apenas o nome da última conta guardada. A sessão expirada por HTTP 401 atualiza a faixa. Uma falha de rede não termina, por si só, a sessão.

Esta versão inclui a sincronização de fotografias por mudança de grupo da 0.1.11. Atualização apenas do cliente, sem alteração do servidor, base de dados ou identificador Android.

Verificação: testes da interface QML em desktop e dimensões Android, incluindo autenticação versus identidade guardada, botões na mesma linha, ícones e acessibilidade, abrir/fechar/confirmar/cancelar observações e validação NOK. Inspeção visual a 320 e 390 px de largura. Cliente C++ compilado para Linux com Qt 6.10.2 para os testes de integração HTTP locais; kit de distribuição continua a requerer Qt 6.12. A compilação Android e a interação no equipamento físico devem ser verificadas no Qt Creator.
